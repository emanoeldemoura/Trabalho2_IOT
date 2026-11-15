// VacinaSegura - Worker (histórico no D1, config no KV e painel)
// POST /leituras, POST /alertas, GET /config/:id  -> INGEST_TOKEN
// PUT /config/:id -> ADMIN_TOKEN
// GET /historico, /resumo, /alertas -> painel

import { rodarPonte } from "./ponte";

const TOPICO_BASE = "sis1a/emanoel_iot/posto01/geladeira01";

export interface Env {
  DB: D1Database;
  KV: KVNamespace;
  INGEST_TOKEN: string;
  ADMIN_TOKEN: string;
}

// padrão se o KV estiver vazio
const CONFIG_PADRAO = {
  t_max: 8, t_min: 2, t_desliga_max: 7, t_desliga_min: 3,
  tempo_alarme_s: 300, tempo_porta_s: 60,
};

const DISPOSITIVO_PADRAO = "geladeira01";
const MAX_LOTE = 500;
const MAX_CORPO = 256 * 1024;
const DIAS_GUARDAR = 90;

// utilidades
function json(dados: unknown, status = 200): Response {
  // sem Access-Control-Allow-Origin: só o próprio domínio lê
  return new Response(JSON.stringify(dados), {
    status,
    headers: { "content-type": "application/json; charset=utf-8" },
  });
}

function tokenOk(req: Request, esperado: string | undefined): boolean {
  if (!esperado) return false;
  const h = req.headers.get("authorization") || "";
  if (!h.startsWith("Bearer ")) return false;
  const a = new TextEncoder().encode(h.slice(7));
  const b = new TextEncoder().encode(esperado);
  if (a.byteLength !== b.byteLength) return false;
  return crypto.subtle.timingSafeEqual(a, b);
}

async function lerJson(req: Request): Promise<unknown> {
  const tam = Number(req.headers.get("content-length") || "0");
  if (tam > MAX_CORPO) throw new Error("corpo grande demais");
  const txt = await req.text();
  if (txt.length > MAX_CORPO) throw new Error("corpo grande demais");
  return JSON.parse(txt);
}

const agora = () => Math.floor(Date.now() / 1000);
const ehNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const idValido = (s: unknown): s is string => typeof s === "string" && /^[a-z0-9_-]{1,32}$/.test(s);

// depois de 2024 e no máximo 5 min no futuro
const tsValido = (t: unknown): t is number =>
  ehNum(t) && Number.isInteger(t) && t >= 1704067200 && t <= agora() + 300;

// validação
type Leitura = { dispositivo: string; ts: number; temp_c: number | null; umid_pct: number | null; porta_aberta: number };

function validarLeitura(o: any): Leitura | null {
  if (!o || typeof o !== "object") return null;
  const disp = o.id ?? o.dispositivo ?? DISPOSITIVO_PADRAO;
  if (!idValido(disp) || !tsValido(o.ts)) return null;
  // null ou faixa do DHT22
  if (o.temp_c !== null && o.temp_c !== undefined && !(ehNum(o.temp_c) && o.temp_c >= -40 && o.temp_c <= 80)) return null;
  if (o.umid_pct !== null && o.umid_pct !== undefined && !(ehNum(o.umid_pct) && o.umid_pct >= 0 && o.umid_pct <= 100)) return null;
  if (typeof o.porta_aberta !== "boolean" && o.porta_aberta !== 0 && o.porta_aberta !== 1) return null;
  return {
    dispositivo: disp, ts: o.ts,
    temp_c: ehNum(o.temp_c) ? o.temp_c : null,
    umid_pct: ehNum(o.umid_pct) ? o.umid_pct : null,
    porta_aberta: o.porta_aberta ? 1 : 0,
  };
}

function validarConfig(c: any): string | null {
  const campos = ["t_max", "t_min", "t_desliga_max", "t_desliga_min", "tempo_alarme_s", "tempo_porta_s"];
  for (const k of campos) if (!ehNum(c?.[k])) return `campo ${k} ausente ou inválido`;
  if (!(c.t_min < c.t_desliga_min && c.t_desliga_min < c.t_desliga_max && c.t_desliga_max < c.t_max))
    return "precisa: t_min < t_desliga_min < t_desliga_max < t_max";
  if (c.t_min < -30 || c.t_max > 30) return "temperaturas fora do plausível (-30..30 °C)";
  for (const k of ["tempo_alarme_s", "tempo_porta_s"])
    if (!Number.isInteger(c[k]) || c[k] < 5 || c[k] > 3600) return `${k} deve ser inteiro entre 5 e 3600`;
  return null;
}

function horasDe(url: URL): number {
  const h = Number(url.searchParams.get("horas") ?? "24");
  return Number.isFinite(h) ? Math.min(168, Math.max(1, Math.floor(h))) : 24;
}
function dispositivoDe(url: URL): string | null {
  const d = url.searchParams.get("dispositivo") ?? DISPOSITIVO_PADRAO;
  return idValido(d) ? d : null;
}

// rotas
async function postLeituras(req: Request, env: Env): Promise<Response> {
  let corpo: any;
  try { corpo = await lerJson(req); } catch { return json({ erro: "JSON inválido" }, 400); }
  const lista: any[] = Array.isArray(corpo) ? corpo : Array.isArray(corpo?.leituras) ? corpo.leituras : [corpo];
  if (lista.length === 0 || lista.length > MAX_LOTE) return json({ erro: `envie de 1 a ${MAX_LOTE} leituras` }, 400);
  return json(await gravarLeituras(env, lista));
}

async function gravarLeituras(env: Env, lista: any[]) {
  const validas: Leitura[] = [];
  let rejeitadas = 0;
  for (const item of lista) {
    const l = validarLeitura(item);
    if (l) validas.push(l); else rejeitadas++;
  }
  // grava as válidas e conta as rejeitadas
  let gravadas = 0;
  if (validas.length > 0) {
    const st = env.DB.prepare(
      "INSERT OR IGNORE INTO leituras (dispositivo, ts, temp_c, umid_pct, porta_aberta) VALUES (?1, ?2, ?3, ?4, ?5)");
    const res = await env.DB.batch(validas.map(l => st.bind(l.dispositivo, l.ts, l.temp_c, l.umid_pct, l.porta_aberta)));
    gravadas = res.reduce((soma, r) => soma + (r.meta.changes ?? 0), 0);
  }
  return { recebidas: lista.length, gravadas, duplicadas: validas.length - gravadas, rejeitadas };
}

// evento: inicio | fim | atendido
async function postAlertas(req: Request, env: Env): Promise<Response> {
  let a: any;
  try { a = await lerJson(req); } catch { return json({ erro: "JSON inválido" }, 400); }
  const r = await processarEvento(env, a);
  return json(r.corpo, r.status);
}

async function processarEvento(env: Env, a: any): Promise<{ status: number; corpo: any }> {
  const json = (corpo: any, status = 200) => ({ status, corpo });
  const tipos = ["temperatura", "porta", "sensor_falha"];
  const disp = a?.id ?? a?.dispositivo ?? DISPOSITIVO_PADRAO;
  const evento = a?.evento ?? "inicio";
  if (!idValido(disp) || !tipos.includes(a?.tipo)) return json({ erro: "tipo ou dispositivo inválido" }, 400);

  if (evento === "inicio") {
    const ini = a.desde_ts ?? a.ts;
    if (!tsValido(ini)) return json({ erro: "desde_ts inválido" }, 400);
    const temp = ehNum(a.temp_c) && a.temp_c >= -40 && a.temp_c <= 80 ? a.temp_c : null;
    // evita duplicar se o lote for reenviado
    const r = await env.DB.prepare(
      `INSERT INTO alertas (dispositivo, tipo, ts_inicio, temp_max_c)
       SELECT ?1, ?2, ?3, ?4 WHERE NOT EXISTS (
         SELECT 1 FROM alertas WHERE dispositivo = ?1 AND tipo = ?2 AND ts_inicio = ?3)`)
      .bind(disp, a.tipo, ini, temp).run();
    return json({ ok: true, id: r.meta.last_row_id, novo: r.meta.changes > 0 });
  }
  if (evento === "fim" || evento === "atendido") {
    const ts = a.ts ?? agora();
    if (!tsValido(ts)) return json({ erro: "ts inválido" }, 400);
    const coluna = evento === "fim" ? "ts_fim" : "ts_atendido";
    const temp = evento === "fim" && ehNum(a.temp_c) && a.temp_c >= -40 && a.temp_c <= 80 ? a.temp_c : null;
    const r = await env.DB.prepare(
      `UPDATE alertas SET ${coluna} = ?1, temp_max_c = COALESCE(?4, temp_max_c) WHERE id = (
         SELECT id FROM alertas WHERE dispositivo = ?2 AND tipo = ?3 AND ts_fim IS NULL AND ${coluna} IS NULL
         ORDER BY ts_inicio DESC LIMIT 1)`)
      .bind(ts, disp, a.tipo, temp).run();
    return json({ ok: true, atualizados: r.meta.changes });
  }
  return json({ erro: "evento deve ser inicio, fim ou atendido" }, 400);
}

async function getConfig(id: string, env: Env): Promise<Response> {
  const salvo = await env.KV.get(`config:${id}`, "json");
  return json(salvo ?? CONFIG_PADRAO);
}

async function putConfig(req: Request, id: string, env: Env): Promise<Response> {
  let c: any;
  try { c = await lerJson(req); } catch { return json({ erro: "JSON inválido" }, 400); }
  const erro = validarConfig(c);
  if (erro) return json({ erro }, 400);
  const limpo = {
    t_max: c.t_max, t_min: c.t_min, t_desliga_max: c.t_desliga_max, t_desliga_min: c.t_desliga_min,
    tempo_alarme_s: c.tempo_alarme_s, tempo_porta_s: c.tempo_porta_s,
  };
  await env.KV.put(`config:${id}`, JSON.stringify(limpo));
  return json({ ok: true, config: limpo });
}

// média por minuto
async function getHistorico(url: URL, env: Env): Promise<Response> {
  const disp = dispositivoDe(url);
  if (!disp) return json({ erro: "dispositivo inválido" }, 400);
  const desde = agora() - horasDe(url) * 3600;
  const { results } = await env.DB.prepare(
    `SELECT (ts / 60) * 60 AS ts, ROUND(AVG(temp_c), 1) AS temp_c, ROUND(AVG(umid_pct)) AS umid_pct,
            MAX(porta_aberta) AS porta_aberta
       FROM leituras WHERE dispositivo = ?1 AND ts >= ?2
      GROUP BY ts / 60 ORDER BY ts`).bind(disp, desde).all();
  return json({ dispositivo: disp, desde, pontos: results });
}

// máx, mín e minutos fora da faixa por hora
async function getResumo(url: URL, env: Env): Promise<Response> {
  const disp = dispositivoDe(url);
  if (!disp) return json({ erro: "dispositivo inválido" }, 400);
  const desde = agora() - horasDe(url) * 3600;
  const cfg: any = (await env.KV.get(`config:${disp}`, "json")) ?? CONFIG_PADRAO;
  const { results } = await env.DB.prepare(
    `SELECT (ts / 3600) * 3600 AS hora_ts,
            MAX(temp_c) AS t_max, MIN(temp_c) AS t_min,
            COUNT(DISTINCT CASE WHEN temp_c > ?3 OR temp_c < ?4 THEN ts / 60 END) AS minutos_fora,
            COUNT(*) AS leituras
       FROM leituras WHERE dispositivo = ?1 AND ts >= ?2
      GROUP BY hora_ts ORDER BY hora_ts`).bind(disp, desde, cfg.t_max, cfg.t_min).all();
  return json({ dispositivo: disp, faixa: [cfg.t_min, cfg.t_max], horas: results });
}

async function getAlertas(url: URL, env: Env): Promise<Response> {
  const disp = dispositivoDe(url);
  if (!disp) return json({ erro: "dispositivo inválido" }, 400);
  const desde = agora() - horasDe(url) * 3600;
  const { results } = await env.DB.prepare(
    `SELECT id, tipo, ts_inicio, ts_fim, temp_max_c, ts_atendido FROM alertas
      WHERE dispositivo = ?1 AND ts_inicio >= ?2 ORDER BY ts_inicio DESC LIMIT 50`).bind(disp, desde).all();
  return json({ dispositivo: disp, alertas: results });
}

// roteador
export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);
    const { pathname: p } = url;
    const m = req.method;
    try {
      if (p.startsWith("/config/") && m === "GET") {
        // ESP32 (INGEST) ou tela de administração (ADMIN)
        if (!tokenOk(req, env.INGEST_TOKEN) && !tokenOk(req, env.ADMIN_TOKEN)) return json({ erro: "não autorizado" }, 401);
      }
      if ((p === "/leituras" && m === "POST") || (p === "/alertas" && m === "POST")) {
        if (!tokenOk(req, env.INGEST_TOKEN)) return json({ erro: "não autorizado" }, 401);
        if (p === "/leituras") return postLeituras(req, env);
        if (p === "/alertas") return postAlertas(req, env);
      }
      if (p.startsWith("/config/") && m === "PUT") {
        if (!tokenOk(req, env.ADMIN_TOKEN)) return json({ erro: "não autorizado" }, 401);
      }
      if (p.startsWith("/config/")) {
        const id = p.slice("/config/".length);
        if (!idValido(id)) return json({ erro: "id inválido" }, 400);
        return m === "PUT" ? putConfig(req, id, env) : getConfig(id, env);
      }
      if (m === "GET" && p === "/historico") return getHistorico(url, env);
      if (m === "GET" && p === "/resumo") return getResumo(url, env);
      if (m === "GET" && p === "/alertas") return getAlertas(url, env);

      return json({ erro: "rota não encontrada" }, 404);
    } catch (e) {
      console.error(e);
      return json({ erro: "erro interno" }, 500);
    }
  },

  // a cada minuto: ponte MQTT -> D1 | 03:00 UTC: apaga dados com mais de 90 dias
  async scheduled(ev: ScheduledController, env: Env): Promise<void> {
    if (ev.cron === "0 3 * * *") {
      const limite = agora() - DIAS_GUARDAR * 86400;
      await env.DB.batch([
        env.DB.prepare("DELETE FROM leituras WHERE ts < ?1").bind(limite),
        env.DB.prepare("DELETE FROM alertas WHERE ts_inicio < ?1").bind(limite),
      ]);
      return;
    }
    const resultado = await rodarPonte(TOPICO_BASE, env.INGEST_TOKEN, {
      gravarLote: async (lote: any) => {
        const leituras = Array.isArray(lote?.leituras) ? lote.leituras.slice(0, MAX_LOTE) : [];
        const r = leituras.length ? await gravarLeituras(env, leituras) : { gravadas: 0, rejeitadas: 0 };
        let eventos = 0;
        for (const e of Array.isArray(lote?.eventos) ? lote.eventos.slice(0, 50) : []) {
          if ((await processarEvento(env, e)).status === 200) eventos++;
        }
        return { gravadas: r.gravadas, rejeitadas: r.rejeitadas, eventos };
      },
      configAtual: async () => (await env.KV.get(`config:${DISPOSITIVO_PADRAO}`, "json")) ?? CONFIG_PADRAO,
    });
    console.log("ponte:", resultado);
  },
} satisfies ExportedHandler<Env>;
