import { useEffect, useRef, useState } from "react";
import mqtt from "mqtt";

// ---------- Configuração ----------
const BROKER = "wss://broker.hivemq.com:8884/mqtt"; // WebSocket seguro do HiveMQ
const TOPIC_PEDIDOS = "foods/grupo_k7x2/pedidos";   // web -> ESP32
const TOPIC_STATUS = "foods/grupo_k7x2/status";     // ESP32 -> web

// Amostra do cardápio de exemplo (acentos são convertidos pelo ESP32 para a impressora)
const CARDAPIO = [
  { nome: "BBB", preco: 36.9 },
  { nome: "BBB bacon", preco: 43.9 },
  { nome: "BBB veg", preco: 36.9 },
  { nome: "Batata frita 250g", preco: 22.9 },
  { nome: "Batata frita 500g", preco: 37.9 },
  { nome: "Pastel de carne", preco: 15.9 },
  { nome: "Pastel de queijo", preco: 14.9 },
  { nome: "Pastel de camarão", preco: 18.9 },
  { nome: "Coca-Cola", preco: 6.9 },
  { nome: "Água mineral", preco: 6.0 },
  { nome: "Chopp Pilsen Kairós 350ml", preco: 12.9 },
  { nome: "Chopp Amstel 350ml", preco: 12.9 },
];

// Mesmos limites validados no ESP32
const MAX_MESA = 99, MAX_QTD = 99, MAX_NOME = 32, MAX_OBS = 80;

const moeda = (v) => "R$ " + v.toFixed(2).replace(".", ",");
const dois = (n) => String(n).padStart(2, "0");

function horarioAtual() {
  const d = new Date();
  return `${dois(d.getDate())}/${dois(d.getMonth() + 1)}/${d.getFullYear()} ${dois(d.getHours())}:${dois(d.getMinutes())}`;
}

const TEXTO_STATUS = {
  enviado: "enviado, aguardando impressão…",
  impresso: "impresso ✓",
  recusado: "recusado",
};

export default function App() {
  const [conectado, setConectado] = useState(false);
  const [mesa, setMesa] = useState("");
  const [cliente, setCliente] = useState("");
  const [obs, setObs] = useState("");
  const [qtds, setQtds] = useState(() => CARDAPIO.map(() => 0));
  const [msg, setMsg] = useState(null); // { texto, tipo: "ok" | "erro" }
  const [historico, setHistorico] = useState([]); // { id, linha, status, motivo }
  const client = useRef(null);

  useEffect(() => {
    const c = mqtt.connect(BROKER, {
      clientId: "web_" + Math.random().toString(16).slice(2, 10),
      reconnectPeriod: 3000,
    });

    c.on("connect", () => {
      setConectado(true);
      c.subscribe(TOPIC_STATUS, { qos: 1 }); // respostas do ESP32
    });
    c.on("close", () => setConectado(false));
    c.on("error", (e) => setMsg({ texto: "Erro MQTT: " + e.message, tipo: "erro" }));

    // Atualiza o histórico quando o ESP32 responde sobre um pedido
    c.on("message", (_topic, payload) => {
      try {
        const { id, status, motivo } = JSON.parse(payload.toString());
        setHistorico((h) => h.map((p) => (p.id === id ? { ...p, status, motivo } : p)));
      } catch {
        /* ignora mensagens que não são JSON */
      }
    });

    client.current = c;
    return () => c.end();
  }, []);

  const total = CARDAPIO.reduce((s, it, i) => s + it.preco * qtds[i], 0);

  const mudarQtd = (i, d) =>
    setQtds((q) => q.map((v, j) => (j === i ? Math.min(MAX_QTD, Math.max(0, v + d)) : v)));

  const limpar = () => {
    setQtds(CARDAPIO.map(() => 0));
    setMesa("");
    setCliente("");
    setObs("");
  };

  function enviar() {
    const n = parseInt(mesa, 10);
    if (!Number.isInteger(n) || n < 1 || n > MAX_MESA) {
      return setMsg({ texto: `Informe a mesa (1 a ${MAX_MESA}).`, tipo: "erro" });
    }

    const itens = CARDAPIO
      .map((it, i) => ({ nome: it.nome, qtd: qtds[i], preco: it.preco }))
      .filter((it) => it.qtd > 0);
    if (itens.length === 0) {
      return setMsg({ texto: "Selecione ao menos um item.", tipo: "erro" });
    }

    const agora = new Date();
    const totalPedido = Math.round(itens.reduce((s, it) => s + it.qtd * it.preco, 0) * 100) / 100;
    const pedido = {
      id: Date.now().toString(36).slice(-5).toUpperCase(), // ex.: "K3F9A"
      mesa: n,
      cliente: cliente.trim().slice(0, MAX_NOME) || "Sem nome",
      obs: obs.trim().slice(0, MAX_OBS),
      ts: agora.getTime(),
      horario: horarioAtual(),
      itens,
      total: totalPedido,
    };

    client.current.publish(TOPIC_PEDIDOS, JSON.stringify(pedido), { qos: 1, retain: false }, (err) => {
      if (err) return setMsg({ texto: "Falha ao enviar: " + err.message, tipo: "erro" });

      const linha = `#${pedido.id} · ${pedido.horario.slice(-5)} · Mesa ${pedido.mesa} · ${pedido.cliente} · ${moeda(totalPedido)}`;
      setHistorico((h) => [{ id: pedido.id, linha, status: "enviado" }, ...h]);
      setMsg({ texto: `Pedido #${pedido.id} enviado!`, tipo: "ok" });
      limpar();
    });
  }

  return (
    <>
      <header>
        <h1>Novo Pedido</h1>
        <span className={"status " + (conectado ? "on" : "off")}>
          {conectado ? "conectado" : "desconectado"}
        </span>
      </header>

      <main>
        <section className="card">
          <div className="row">
            <label>
              Mesa
              <input
                type="number" min="1" max={MAX_MESA} inputMode="numeric" placeholder="1 a 99"
                value={mesa} onChange={(e) => setMesa(e.target.value)}
              />
            </label>
            <label>
              Cliente
              <input
                type="text" maxLength={MAX_NOME} placeholder="Nome do cliente"
                value={cliente} onChange={(e) => setCliente(e.target.value)}
              />
            </label>
          </div>
        </section>

        <section className="card">
          <h2>Cardápio</h2>
          {CARDAPIO.map((it, i) => (
            <div className="item" key={it.nome}>
              <span className="nome">{it.nome}</span>
              <span className="preco">{moeda(it.preco)}</span>
              <div className="qtd">
                <button type="button" onClick={() => mudarQtd(i, -1)}>−</button>
                <span>{qtds[i]}</span>
                <button type="button" onClick={() => mudarQtd(i, 1)}>+</button>
              </div>
            </div>
          ))}
        </section>

        <section className="card">
          <label>
            Observações
            <input
              type="text" maxLength={MAX_OBS} placeholder="Ex.: sem cebola"
              value={obs} onChange={(e) => setObs(e.target.value)}
            />
          </label>
        </section>

        <section className="card">
          <div className="total">Total: <strong>{moeda(total)}</strong></div>
          <div className="acoes">
            <button className="sec" onClick={() => { limpar(); setMsg(null); }}>Limpar</button>
            <button id="enviar" onClick={enviar} disabled={!conectado}>Enviar pedido</button>
          </div>
          <p className={"msg " + (msg?.tipo ?? "")}>{msg?.texto}</p>
        </section>

        <section className="card">
          <h2>Enviados nesta sessão</h2>
          <ul id="historico">
            {historico.length === 0 ? (
              <li className="vazio">Nenhum pedido enviado ainda.</li>
            ) : (
              historico.map((p) => (
                <li key={p.id}>
                  {p.linha} —{" "}
                  <strong className={"st " + p.status}>
                    {TEXTO_STATUS[p.status] ?? p.status}
                    {p.motivo ? `: ${p.motivo}` : ""}
                  </strong>
                </li>
              ))
            )}
          </ul>
        </section>
      </main>
    </>
  );
}
