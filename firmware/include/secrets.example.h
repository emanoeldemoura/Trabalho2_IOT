// MODELO de segredos. Copie para "secrets.h" e preencha com os valores reais.
// O arquivo secrets.h está no .gitignore e NUNCA deve ir para o GitHub.
#pragma once

#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_SENHA    ""

// Broker MQTT da aula: test.mosquitto.org, porta 8886 = TLS (Let's Encrypt)
#define MQTT_HOST     "test.mosquitto.org"
#define MQTT_PORTA    8886

// Senha que assina os comandos do painel (HMAC). Invente uma longa.
#define COMANDO_SENHA "troque-por-uma-senha-longa"

#define WORKER_URL    "https://preencher.workers.dev"
#define INGEST_TOKEN  "preencher"
