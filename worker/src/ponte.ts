// Ponte MQTT -> D1 (cron a cada minuto): lê <base>/lote, grava no D1,
// responde em <base>/lote_ack e publica a config em <base>/config.
// Mensagens: "<HMAC-SHA256(INGEST_TOKEN, json)>\n<json>"
import { connect } from "cloudflare:sockets";

const BROKER_HOST = "test.mosquitto.org";
const BROKER_PORTA = 8886;
const TEMPO_MAX_MS = 40_000;
const TEMPO_OCIOSO_MS = 6_000;

// assinatura (HMAC)
async function chave(segredo: string): Promise<CryptoKey> {
  return crypto.subtle.importKey("raw", new TextEncoder().encode(segredo),
    { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}
const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map(x => x.toString(16).padStart(2, "0")).join("");
function deHex(h: string): Uint8Array | null {
  if (!/^[0-9a-f]{64}$/.test(h)) return null;
  const out = new Uint8Array(32);
  for (let i = 0; i < 32; i++) out[i] = parseInt(h.slice(2 * i, 2 * i + 2), 16);
  return out;
}
export async function assinar(segredo: string, texto: string): Promise<string> {
  return hex(await crypto.subtle.sign("HMAC", await chave(segredo), new TextEncoder().encode(texto)));
}
export async function conferir(segredo: string, mensagem: string): Promise<string | null> {
  const nl = mensagem.indexOf("\n");
  if (nl !== 64) return null;
  const sig = deHex(mensagem.slice(0, 64));
  const json = mensagem.slice(65);
  if (!sig) return null;
  const ok = await crypto.subtle.verify("HMAC", await chave(segredo), sig, new TextEncoder().encode(json));
  return ok ? json : null;
}

// MQTT 3.1.1 mínimo
const enc = new TextEncoder();
const dec = new TextDecoder();

function tamanhoRestante(n: number): number[] {
  const bytes: number[] = [];
  do { let d = n % 128; n = Math.floor(n / 128); if (n > 0) d |= 0x80; bytes.push(d); } while (n > 0);
  return bytes;
}
function str(s: string): number[] { const b = enc.encode(s); return [b.length >> 8, b.length & 0xff, ...b]; }
function pacote(tipo: number, corpo: number[]): Uint8Array { return new Uint8Array([tipo, ...tamanhoRestante(corpo.length), ...corpo]); }

const pktConnect = (clientId: string) =>
  pacote(0x10, [...str("MQTT"), 0x04, 0x02 /* clean session */, 0x00, 0x3c /* keepalive 60 s */, ...str(clientId)]);
const pktSubscribe = (id: number, topicos: string[]) =>
  pacote(0x82, [id >> 8, id & 0xff, ...topicos.flatMap(t => [...str(t), 0x00])]);
function pktPublish(topico: string, texto: string, retido: boolean): Uint8Array {
  return pacote(0x30 | (retido ? 1 : 0), [...str(topico), ...enc.encode(texto)]);
}
const pktDisconnect = () => new Uint8Array([0xe0, 0x00]);

type Msg = { tipo: number; topico?: string; texto?: string };

class LeitorMqtt {
  private buf = new Uint8Array(0);
  // leitura pendente é reaproveitada se o prazo estourar
  private pendente: Promise<ReadableStreamReadResult<Uint8Array>> | null = null;
  constructor(private r: ReadableStreamDefaultReader<Uint8Array>) {}
  async cancelar() { try { await this.r.cancel(); } catch { /* ok */ } }
  private tenta(): Msg | null {
    const b = this.buf;
    if (b.length < 2) return null;
    let mult = 1, rest = 0, i = 1, d: number;
    do { if (i >= b.length) return null; d = b[i++]; rest += (d & 0x7f) * mult; mult *= 128; } while (d & 0x80);
    if (b.length < i + rest) return null;
    const corpo = b.slice(i, i + rest);
    const tipo = b[0] >> 4;
    this.buf = b.slice(i + rest);
    if (tipo === 3) {               // PUBLISH
      const tl = (corpo[0] << 8) | corpo[1];
      let p = 2 + tl;
      if ((b[0] >> 1) & 3) p += 2;
      return { tipo, topico: dec.decode(corpo.slice(2, 2 + tl)), texto: dec.decode(corpo.slice(p)) };
    }
    return { tipo };
  }
  async proximo(prazoMs: number): Promise<Msg | null> {
    for (;;) {
      const m = this.tenta();
      if (m) return m;
      const espera = prazoMs - Date.now();
      if (espera <= 0) return null;
      if (!this.pendente) this.pendente = this.r.read();
      const PRAZO = Symbol("prazo");
      let timer: any;
      const r = await Promise.race([
        this.pendente,
        new Promise<typeof PRAZO>(res => { timer = setTimeout(() => res(PRAZO), espera); }),
      ]);
      clearTimeout(timer);
      if (r === PRAZO) return null;
      this.pendente = null;
      if (r.done || !r.value) return null;
      const novo = new Uint8Array(this.buf.length + r.value.length);
      novo.set(this.buf); novo.set(r.value, this.buf.length);
      this.buf = novo;
    }
  }
}

// rodada da ponte
export interface Gravador {
  gravarLote(lote: any): Promise<{ gravadas: number; rejeitadas: number; eventos: number }>;
  configAtual(): Promise<object>;
}

export async function rodarPonte(base: string, segredo: string, g: Gravador): Promise<string> {
  const sock = connect({ hostname: BROKER_HOST, port: BROKER_PORTA }, { secureTransport: "on" });
  const w = sock.writable.getWriter();
  const leitor = new LeitorMqtt(sock.readable.getReader());
  const log: string[] = [];
  const fim = Date.now() + TEMPO_MAX_MS;
  try {
    await w.write(pktConnect("vacinasegura-worker-" + crypto.randomUUID().slice(0, 8)));
    const connack = await leitor.proximo(Date.now() + 10_000);
    if (!connack || connack.tipo !== 2) throw new Error("broker não respondeu CONNACK");
    console.log("ponte: conectado ao broker");

    const cfg = JSON.stringify({ ...(await g.configAtual()), ts: Math.floor(Date.now() / 1000) });
    await w.write(pktPublish(`${base}/config`, `${await assinar(segredo, cfg)}\n${cfg}`, true));

    console.log("ponte: config publicada");
    await w.write(pktSubscribe(1, [`${base}/lote`]));
    const vistos = new Set<string>();
    let prazo = Date.now() + TEMPO_OCIOSO_MS;
    while (Date.now() < fim) {
      const m = await leitor.proximo(Math.min(prazo, fim));
      if (!m) break;
      if (m.tipo !== 3 || m.topico !== `${base}/lote` || !m.texto) continue;
      console.log(`ponte: mensagem em /lote (${m.texto.length} bytes)`);
      prazo = Date.now() + TEMPO_OCIOSO_MS;
      const json = await conferir(segredo, m.texto);
      if (!json) { log.push("lote com assinatura inválida: ignorado"); continue; }
      let lote: any;
      try { lote = JSON.parse(json); } catch { log.push("lote com JSON inválido"); continue; }
      const id = String(lote?.id ?? "");
      if (!/^[A-Za-z0-9_-]{1,32}$/.test(id) || vistos.has(id)) continue;
      vistos.add(id);
      const r = await g.gravarLote(lote);
      log.push(`lote ${id}: ${r.gravadas} leituras, ${r.eventos} eventos, ${r.rejeitadas} rejeitadas`);
      const ack = JSON.stringify({ id, ok: true, gravadas: r.gravadas });
      await w.write(pktPublish(`${base}/lote_ack`, `${await assinar(segredo, ack)}\n${ack}`, false));
    }
    await w.write(pktDisconnect());
  } finally {
    await leitor.cancelar();
    try { w.releaseLock(); } catch { /* ok */ }
    await Promise.race([sock.close().catch(() => {}), new Promise(r => setTimeout(r, 2000))]);
  }
  return log.length ? log.join(" | ") : "nenhum lote";
}
