-- VacinaSegura - banco D1 (histórico)
-- Rodar com: npx wrangler d1 execute vacinasegura --remote --file=schema.sql

CREATE TABLE IF NOT EXISTS leituras (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dispositivo TEXT NOT NULL,
  ts INTEGER NOT NULL,          -- epoch UTC (segundos)
  temp_c REAL,                  -- NULL se o sensor falhou
  umid_pct REAL,
  porta_aberta INTEGER NOT NULL -- 0/1
);
-- único: lote reenviado não duplica
CREATE UNIQUE INDEX IF NOT EXISTS idx_leituras_disp_ts ON leituras(dispositivo, ts);

CREATE TABLE IF NOT EXISTS alertas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dispositivo TEXT NOT NULL,
  tipo TEXT NOT NULL,           -- temperatura | porta | sensor_falha
  ts_inicio INTEGER NOT NULL,
  ts_fim INTEGER,
  temp_max_c REAL,
  ts_atendido INTEGER           -- só o horário; NÃO guardar quem atendeu (LGPD)
);
CREATE INDEX IF NOT EXISTS idx_alertas_disp_ts ON alertas(dispositivo, ts_inicio);
