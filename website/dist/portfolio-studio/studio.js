export const PORTFOLIO_SCHEMA = "blacklion.asset-archive.web-portfolio.v1";
const ID_PATTERN = /^[A-Za-z0-9_-]+$/;
const ALLOWED_FIELDS = new Set(["id", "name", "visibility", "model_glb", "poster", "description", "alt", "license", "attribution"]);

export function isLocalOrigin(locationLike) {
  return locationLike?.protocol === "http:" && ["localhost", "127.0.0.1", "::1"].includes(locationLike.hostname);
}

export function validateSelection(value) {
  if (!value || typeof value !== "object" || value.schema !== PORTFOLIO_SCHEMA) {
    throw new Error("선택 파일의 스키마가 올바르지 않습니다.");
  }
  if (typeof value.title !== "string" || !value.title.trim() || value.title.length > 120 || !Array.isArray(value.assets) || value.assets.length < 1 || value.assets.length > 100) {
    throw new Error("제목은 1~120자, 오브젝트는 1~100개여야 합니다.");
  }
  const ids = new Set();
  for (const item of value.assets) {
    if (!item || typeof item !== "object" || Object.keys(item).some(key => !ALLOWED_FIELDS.has(key))) {
      throw new Error("지원하지 않는 항목이 포함되어 있습니다.");
    }
    if (typeof item.id !== "string" || !ID_PATTERN.test(item.id) || ids.has(item.id)) {
      throw new Error("오브젝트 ID가 잘못되었거나 중복되었습니다.");
    }
    ids.add(item.id);
    if (typeof item.name !== "string" || !item.name.trim() || item.name.length > 120) {
      throw new Error(`오브젝트 ${item.id}의 이름이 올바르지 않습니다.`);
    }
    if (item.visibility !== "public" && item.visibility !== "private") {
      throw new Error(`오브젝트 ${item.id}의 공개 범위는 public 또는 private이어야 합니다.`);
    }
    for (const field of ["model_glb", "poster"]) {
      if (typeof item[field] !== "string" || !item[field].trim()) throw new Error(`오브젝트 ${item.id}에 ${field} 경로가 없습니다.`);
    }
    for (const field of ["description", "alt", "license", "attribution"]) {
      if (item[field] !== undefined && (typeof item[field] !== "string" || item[field].length > 1200)) {
        throw new Error(`오브젝트 ${item.id}의 ${field} 값이 올바르지 않습니다.`);
      }
    }
  }
  return value;
}

export function serializeSelection(value) {
  return `${JSON.stringify(validateSelection(value), null, 2)}\n`;
}

function node(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text) element.textContent = text;
  return element;
}

function setStatus(element, message, kind = "") {
  element.textContent = message;
  if (kind) element.dataset.kind = kind;
  else delete element.dataset.kind;
}

function initStudio() {
  const local = isLocalOrigin(window.location);
  const localNotice = document.getElementById("local-only-notice");
  const openButton = document.getElementById("open-file");
  const inputLabel = document.getElementById("fallback-picker");
  const input = document.getElementById("file-input");
  const fileMeta = document.getElementById("file-meta");
  const list = document.getElementById("asset-list");
  const summary = document.getElementById("summary");
  const total = document.getElementById("total-count");
  const publicCount = document.getElementById("public-count");
  const privateCount = document.getElementById("private-count");
  const saveButton = document.getElementById("save-file");
  const downloadButton = document.getElementById("download-file");
  const status = document.getElementById("status");
  let selection = null;
  let fileHandle = null;
  let fileName = "portfolio.local.json";
  let dirty = false;

  if (!local) {
    localNotice.hidden = false;
    openButton.disabled = true;
    inputLabel.hidden = true;
    setStatus(status, "로컬 주소가 아니어서 파일 접근과 수정을 사용할 수 없습니다.", "error");
    return;
  }

  const canWriteHandle = typeof window.showOpenFilePicker === "function";
  inputLabel.hidden = canWriteHandle;
  saveButton.hidden = !canWriteHandle;

  function markChanged() {
    dirty = true;
    downloadButton.disabled = false;
    saveButton.disabled = !fileHandle;
    setStatus(status, "공개 범위가 바뀌었습니다. 저장 전에는 파일이 변경되지 않습니다.");
    updateSummary();
  }

  function updateSummary() {
    if (!selection) return;
    const pub = selection.assets.filter(asset => asset.visibility === "public").length;
    total.textContent = String(selection.assets.length);
    publicCount.textContent = String(pub);
    privateCount.textContent = String(selection.assets.length - pub);
  }

  function render() {
    list.replaceChildren();
    if (!selection) {
      summary.hidden = true;
      list.append(node("p", "empty-state", "선택 파일을 열면 등록된 오브젝트가 여기에 표시됩니다."));
      return;
    }
    summary.hidden = false;
    updateSummary();
    for (const asset of selection.assets) {
      const row = node("article", "asset-row");
      const nameBlock = node("div", "asset-name-block");
      nameBlock.append(node("span", "asset-name", asset.name), node("span", "asset-id", asset.id));
      const setting = node("label", "asset-visibility", "웹 공개 범위");
      const select = document.createElement("select");
      select.id = `visibility-${asset.id}`;
      select.setAttribute("aria-label", `${asset.name} 웹 공개 범위`);
      for (const [value, label] of [["private", "비공개 · 이 PC에 유지"], ["public", "공개 · 웹 파생본에 포함"]]) {
        const option = document.createElement("option");
        option.value = value;
        option.textContent = label;
        option.selected = asset.visibility === value;
        select.append(option);
      }
      select.addEventListener("change", () => {
        asset.visibility = select.value;
        markChanged();
      });
      setting.append(select);
      row.append(nameBlock, setting);
      list.append(row);
    }
  }

  async function acceptFile(file, handle = null) {
    const parsed = JSON.parse(await file.text());
    selection = structuredClone(validateSelection(parsed));
    fileHandle = handle;
    fileName = file.name || "portfolio.local.json";
    dirty = false;
    fileMeta.textContent = `선택한 파일 · ${fileName}`;
    saveButton.disabled = true;
    downloadButton.disabled = true;
    render();
    setStatus(status, fileHandle ? "파일을 열었습니다. 공개 범위를 바꾼 뒤 저장을 누르세요." : "읽기 전용으로 열었습니다. 수정 후 JSON 다운로드를 누르세요.", "success");
  }

  openButton.addEventListener("click", async () => {
    if (!local) return;
    try {
      if (canWriteHandle) {
        const [handle] = await window.showOpenFilePicker({
          multiple: false,
          types: [{ description: "Portfolio selection JSON", accept: { "application/json": [".json"] } }]
        });
        await acceptFile(await handle.getFile(), handle);
      } else {
        input.click();
      }
    } catch (error) {
      if (error?.name !== "AbortError") setStatus(status, `파일을 열지 못했습니다: ${error.message}`, "error");
    }
  });

  input.addEventListener("change", async () => {
    const file = input.files?.[0];
    if (!file) return;
    try { await acceptFile(file); }
    catch (error) { selection = null; fileHandle = null; render(); setStatus(status, `파일 형식을 확인할 수 없습니다: ${error.message}`, "error"); }
    finally { input.value = ""; }
  });

  saveButton.addEventListener("click", async () => {
    if (!local || !fileHandle || !selection || !dirty) return;
    let writable;
    try {
      const serialized = serializeSelection(selection);
      writable = await fileHandle.createWritable();
      await writable.write(serialized);
      await writable.close();
      writable = null;
      dirty = false;
      saveButton.disabled = true;
      downloadButton.disabled = true;
      setStatus(status, `${fileName}에 저장했습니다. GitHub에는 아무 파일도 전송하지 않았습니다.`, "success");
    } catch (error) {
      if (writable) await writable.abort().catch(() => {});
      setStatus(status, `로컬 파일 저장에 실패했습니다: ${error.message}`, "error");
    }
  });

  downloadButton.addEventListener("click", () => {
    if (!local || !selection || !dirty) return;
    try {
      const blob = new Blob([serializeSelection(selection)], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = fileName;
      link.click();
      URL.revokeObjectURL(url);
      dirty = false;
      saveButton.disabled = true;
      downloadButton.disabled = true;
      setStatus(status, "수정한 JSON을 다운로드했습니다. 원본 파일은 그대로 두었습니다.", "success");
    } catch (error) {
      setStatus(status, `JSON 다운로드를 만들지 못했습니다: ${error.message}`, "error");
    }
  });

  render();
}

if (typeof document !== "undefined") initStudio();
