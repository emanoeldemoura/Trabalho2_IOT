# VacinaSegura

Monitor de geladeira de vacinas com ESP32 — Trabalho N2 de IoT (Faculdade Antonio Meneghetti, Sistemas de Informação).

> Para postos de saúde, que sofrem com perda de vacinas por falhas de temperatura que ninguém percebe,
> o VacinaSegura sente a temperatura e a porta, decide se a geladeira saiu da faixa de 2 a 8 °C e age com
> alarme local e alerta remoto, mesmo se a internet cair.

**Painel:** https://vacinasegura.emanoel-moura.workers.dev

## O que o sistema faz

- Lê a temperatura (DHT22) e a porta (ultrassônico HC-SR04).
- Liga buzzer e LED se a temperatura ficar fora de 2–8 °C por um tempo (histerese: só desliga entre 3 e 7 °C).
- Alerta de porta aberta e de falha do sensor (sensor sem resposta = alarme ligado).
- Envia tempo real por MQTT com TLS e guarda o histórico na nuvem (Cloudflare D1).
- Sem internet o alarme continua funcionando e as leituras ficam numa fila; quando a conexão volta, tudo é enviado.
- Painel web com temperatura ao vivo, gráfico, resumo por hora, alertas e o botão "Silenciar" com confirmação.

## Arquitetura

```
ESP32 (Wokwi) ──MQTT/TLS 8886──► test.mosquitto.org ◄──WSS 8081── Painel (navegador)
                                        ▲
                                        │ a cada minuto (lotes assinados)
                               Cloudflare Worker ──► D1 (histórico) / KV (config)
                                        │
                                        └── serve o painel e as rotas /historico, /resumo, /alertas
```

## Pastas

| Pasta | Conteúdo |
|---|---|
| `firmware/` | código do ESP32 (`src/main.cpp`), circuito (`diagram.json`), testes do Wokwi (`scenarios/`) |
| `worker/` | Worker da Cloudflare (`src/`), banco (`schema.sql`) e painel (`public/index.html`) |
| `docs/` | caderno do projeto (`caderno.pdf`) |

## Como rodar no Wokwi (site)

1. Crie um projeto ESP32 em https://wokwi.com.
2. Cole `firmware/src/main.cpp` na aba `sketch.ino` e `firmware/diagram.json` na aba `diagram.json`.
3. Crie a aba `secrets.h` a partir de `firmware/include/secrets.example.h` e preencha os valores.
4. No Library Manager adicione as bibliotecas de `firmware/libraries.txt`.
5. Rode e abra o painel.

## Como rodar pelo VS Code

- Firmware: PlatformIO (`cd firmware && pio run`) e simulação com `wokwi-cli --timeout 30000`.
- Worker: `cd worker && npx wrangler deploy`
  (segredos com `npx wrangler secret put INGEST_TOKEN` e `ADMIN_TOKEN`; tabelas com `schema.sql`).

## Testes

| Teste | Como |
|---|---|
| Alarme | 10 °C por 20 s liga; 6 °C desliga (`scenarios/fase2.yaml`) |
| Ataque | comando sem assinatura, falso ou repetido é ignorado (`scenarios/fase3.yaml`) |
| Sem internet | botão azul derruba o Wi-Fi por 60 s; o histórico não fica com buraco (`scenarios/fase5.yaml`) |
| Rota sem token | `curl -X POST <worker>/leituras` → 401 |

## Segurança

- MQTT só com TLS e certificado conferido (`setCACert`, sem `setInsecure`).
- Comandos e lotes do histórico assinados com HMAC-SHA256; o comando usa um nonce que muda a cada uso.
- Rotas de gravação exigem token (401 sem token).
- Senhas e tokens ficam em `secrets.h` e `.dev.vars`, fora do Git.
