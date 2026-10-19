# Foods Pedidos

App web (React) → broker MQTT (HiveMQ) → ESP32 → impressora térmica (ESC/POS).

## Estrutura

```
foods-pedidos/
├── esp32_pedidos/
│   ├── esp32_pedidos.ino    firmware (pasta e .ino com o mesmo nome)
│   └── libraries.txt        bibliotecas para o Wokwi
└── web/
    ├── package.json         dependências
    ├── vite.config.js
    ├── index.html
    └── src/                 main.jsx, App.jsx, style.css
```

## Dependências

**ESP32 (Arduino IDE ou Wokwi)**
- Core ESP32 da Espressif (traz WiFi e BluetoothSerial)
- PubSubClient (Nick O'Leary)
- ArduinoJson 7.x (Benoit Blanchon)

**Web**
- react e react-dom 18, mqtt 5 (MQTT.js)
- vite e @vitejs/plugin-react (desenvolvimento)

## Como rodar

**ESP32**
1. Abra `esp32_pedidos/esp32_pedidos.ino` no Arduino IDE (ou cole o código no Wokwi, com `libraries.txt`).
2. Placa: ESP32 Dev Module. Instale as bibliotecas acima.
3. Impressora real: descomente `#define IMPRESSORA_BT`, ajuste `NOME_IMPRESSORA` e use Partition Scheme "Huge APP (3MB No OTA)".

**Web**
```
cd web
npm install
npm run dev
```

## Protocolo MQTT (broker.hivemq.com)

| Tópico | Sentido | Conteúdo |
|---|---|---|
| `foods/grupo_k7x2/pedidos` | web → ESP32 | pedido (JSON) |
| `foods/grupo_k7x2/status` | ESP32 → web | `{"id":"..","status":"impresso" \| "recusado","motivo":".."}` |

Pedido:
```json
{"id":"K3F9A","mesa":5,"cliente":"João","obs":"sem cebola",
 "ts":1760900000000,"horario":"19/10/2026 22:44",
 "itens":[{"nome":"BBB","qtd":2,"preco":36.9}],"total":73.8}
```
`id` é string. Limites: mesa 1 a 99, até 30 itens, quantidade 1 a 99.

O tópico `grupo_k7x2` precisa ser igual no `.ino` e em `web/src/App.jsx`.
