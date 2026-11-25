const byId = (id) => document.getElementById(id);

function percent(value) {
  return typeof value === "number" ? `${Math.round(value)}%` : "--";
}

function setMetric(name, value) {
  byId(name).textContent = percent(value);
  byId(`${name}-bar`).style.width = typeof value === "number" ? `${Math.max(0, Math.min(100, value))}%` : "0";
}

function render(state) {
  const home = state.home || {};
  const telemetry = state.telemetry || {};
  const connection = state.connection || {};
  byId("brand").textContent = home.brand || "NEXUS";
  byId("project").textContent = home.active_project || "No active project";
  byId("mode").textContent = (home.work_mode || "IDLE").toUpperCase();
  byId("commitment").textContent = home.next_commitment || "Nothing scheduled";
  const tasks = Array.isArray(home.priority_tasks) ? home.priority_tasks.slice(0, 3) : [];
  byId("tasks").replaceChildren(...tasks.map((task) => {
    const item = document.createElement("li");
    item.textContent = String(task);
    return item;
  }));
  byId("task-count").textContent = tasks.length;
  byId("attention").textContent = home.attention_count || 0;
  byId("agents").textContent = Array.isArray(home.agents) ? home.agents.length : 0;
  byId("approvals").textContent = home.pending_approvals || 0;
  const status = connection.status || "offline";
  const connectionNode = byId("connection");
  connectionNode.className = `connection ${status}`;
  connectionNode.lastChild.textContent = status.toUpperCase();
  setMetric("cpu", telemetry.cpu_percent);
  setMetric("memory", telemetry.memory_percent);
  setMetric("disk", telemetry.disk_percent);
  byId("host").textContent = (telemetry.hostname || "LOCAL").toUpperCase();
  const battery = percent(telemetry.battery_percent);
  byId("battery").textContent = telemetry.battery_charging === true ? `${battery} ↯` : battery;
  const repository = telemetry.repository || {};
  byId("branch").textContent = repository.branch || "NO REPO";
  byId("dirty").textContent = repository.available ? (repository.dirty ? "DIRTY" : "CLEAN") : "—";
  byId("dirty").className = repository.dirty ? "dirty" : "clean";
}

function updateClock() {
  const now = new Date();
  byId("time").textContent = now.toLocaleTimeString([], {hour: "2-digit", minute: "2-digit", hour12: false});
  byId("date").textContent = now.toLocaleDateString([], {month: "short", day: "2-digit"}).toUpperCase();
}

async function refresh() {
  try {
    const response = await fetch("/api/state", {cache: "no-store"});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch (_) {
    const node = byId("connection");
    node.className = "connection offline";
    node.lastChild.textContent = "SIM ERROR";
  }
}

updateClock();
refresh();
setInterval(updateClock, 1000);
setInterval(refresh, 750);
