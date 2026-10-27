export const PORTFOLIO_SCHEMA = "blacklion.asset-archive.web-portfolio.v1";
const ID_PATTERN = /^[A-Za-z0-9_-]+$/;
const MODEL_PATTERN = /^assets\/([A-Za-z0-9_-]+)\/model\.glb$/;
const POSTER_PATTERN = /^assets\/([A-Za-z0-9_-]+)\/poster\.(webp|jpe?g|png)$/i;

export function validatePortfolioManifest(value) {
  if (!value || typeof value !== "object" || value.schema !== PORTFOLIO_SCHEMA || value.loading !== "per-asset") {
    throw new Error("포트폴리오 매니페스트 형식이 올바르지 않습니다.");
  }
  if (typeof value.title !== "string" || !value.title.trim() || !Array.isArray(value.assets) || value.assets.length > 100) {
    throw new Error("포트폴리오 제목이나 오브젝트 목록이 올바르지 않습니다.");
  }
  const ids = new Set();
  for (const asset of value.assets) {
    if (!asset || typeof asset !== "object" || !ID_PATTERN.test(asset.id) || ids.has(asset.id)) {
      throw new Error("포트폴리오에 잘못되었거나 중복된 오브젝트 ID가 있습니다.");
    }
    ids.add(asset.id);
    if (typeof asset.name !== "string" || !asset.name.trim() || !MODEL_PATTERN.test(asset.model) || !POSTER_PATTERN.test(asset.poster)) {
      throw new Error(`오브젝트 ${asset.id}의 이름이나 로컬 미디어 경로가 올바르지 않습니다.`);
    }
    if (MODEL_PATTERN.exec(asset.model)[1] !== asset.id || POSTER_PATTERN.exec(asset.poster)[1] !== asset.id) {
      throw new Error(`오브젝트 ${asset.id}의 파일 경로가 해당 오브젝트 폴더를 벗어났습니다.`);
    }
    for (const field of ["description", "alt", "license", "attribution"]) {
      if (asset[field] !== undefined && typeof asset[field] !== "string") {
        throw new Error(`오브젝트 ${asset.id}의 ${field} 값은 문자열이어야 합니다.`);
      }
    }
  }
  return value;
}

export function formatBytes(value) {
  if (!Number.isFinite(value) || value < 0) return "용량 정보 없음";
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(0)} KiB`;
  return `${(value / (1024 * 1024)).toFixed(1)} MiB`;
}

function setStatus(element, text, kind = "") {
  element.textContent = text;
  if (kind) element.dataset.kind = kind;
  else delete element.dataset.kind;
}

function createElement(tag, className, text = "") {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text) element.textContent = text;
  return element;
}

async function loadPortfolio() {
  const grid = document.getElementById("asset-grid");
  const count = document.getElementById("asset-count");
  const status = document.getElementById("load-status");
  const title = document.getElementById("portfolio-title");
  const footerTitle = document.getElementById("footer-title");
  const detail = document.getElementById("object-detail");
  const detailTitle = document.getElementById("detail-title");
  const detailDescription = document.getElementById("detail-description");
  const detailCredits = document.getElementById("detail-credits");
  const loadButton = document.getElementById("load-model");
  const viewerStatus = document.getElementById("viewer-status");
  const viewerStage = document.getElementById("viewer-stage");
  const buttons = [];
  let currentAsset = null;
  let currentViewer = null;
  let viewerLibrary = null;
  let libraryPromise = null;
  let selectionVersion = 0;

  function releaseViewer() {
    if (currentViewer) {
      currentViewer.removeAttribute("src");
      currentViewer.removeAttribute("poster");
      currentViewer.remove();
      currentViewer = null;
    }
    viewerStage.replaceChildren(createPlaceholder());
    setStatus(viewerStatus, "3D 모델을 아직 불러오지 않았습니다.");
  }

  function createPlaceholder() {
    const placeholder = createElement("div", "poster-placeholder");
    const glyph = createElement("span", "poster-glyph", "◈");
    glyph.setAttribute("aria-hidden", "true");
    placeholder.append(glyph, createElement("span", "", "오브젝트를 선택한 다음 3D 미리보기를 불러오세요."));
    return placeholder;
  }

  function selectAsset(asset, selectedButton) {
    selectionVersion += 1;
    currentAsset = asset;
    releaseViewer();
    detail.hidden = false;
    detailTitle.textContent = asset.name;
    detailDescription.textContent = asset.description || "이 포트폴리오에 선별해 등록한 오브젝트입니다.";
    detailCredits.replaceChildren();
    const info = [];
    if (Number.isFinite(asset.model_bytes)) info.push(`최적화된 모델 · ${formatBytes(asset.model_bytes)}`);
    if (asset.license) info.push(`라이선스 · ${asset.license}`);
    if (asset.attribution) info.push(`출처 · ${asset.attribution}`);
    for (const line of info) detailCredits.append(createElement("span", "", line));
    loadButton.disabled = false;
    loadButton.textContent = "3D 미리보기 불러오기";
    for (const button of buttons) button.setAttribute("aria-pressed", String(button === selectedButton));
    detail.scrollIntoView({ behavior: window.matchMedia("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth", block: "nearest" });
  }

  async function ensureViewerLibrary() {
    if (viewerLibrary) return viewerLibrary;
    if (!libraryPromise) {
      libraryPromise = new Promise((resolve, reject) => {
        const existing = document.querySelector("script[data-portfolio-viewer]");
        if (existing) {
          existing.addEventListener("load", () => resolve(true), { once: true });
          existing.addEventListener("error", () => reject(new Error("로컬 3D 뷰어를 불러오지 못했습니다.")), { once: true });
          if (customElements.get("model-viewer")) resolve(true);
          return;
        }
        const script = document.createElement("script");
        script.type = "module";
        script.src = new URL("./vendor/model-viewer.min.js", import.meta.url).href;
        script.dataset.portfolioViewer = "true";
        script.onload = () => resolve(true);
        script.onerror = () => reject(new Error("로컬 3D 뷰어를 불러오지 못했습니다."));
        document.head.append(script);
      });
    }
    viewerLibrary = await libraryPromise;
    if (!customElements.get("model-viewer")) throw new Error("로컬 3D 뷰어를 초기화하지 못했습니다.");
    return viewerLibrary;
  }

  async function loadSelectedModel() {
    if (!currentAsset) return;
    const requestedAsset = currentAsset;
    const requestedVersion = selectionVersion;
    loadButton.disabled = true;
    loadButton.textContent = "뷰어를 불러오는 중…";
    setStatus(viewerStatus, "로컬 3D 뷰어를 불러오고 있습니다…");
    try {
      await ensureViewerLibrary();
      if (!currentAsset || requestedVersion !== selectionVersion || requestedAsset !== currentAsset) return;
      const asset = requestedAsset;
      releaseViewer();
      const viewer = document.createElement("model-viewer");
      viewer.className = "portfolio-model";
      viewer.setAttribute("alt", asset.alt || asset.name);
      viewer.setAttribute("camera-controls", "");
      viewer.setAttribute("touch-action", "pan-y");
      viewer.setAttribute("loading", "eager");
      viewer.setAttribute("reveal", "auto");
      viewer.setAttribute("poster", new URL(asset.poster, manifestUrl).href);
      viewer.addEventListener("load", () => setStatus(viewerStatus, `${asset.name}을(를) 불러왔습니다. 드래그해 회전하고 조작부로 확대·축소하세요.`), { once: true });
      viewer.addEventListener("error", () => setStatus(viewerStatus, "3D 모델을 불러오지 못했습니다. 미리보기 이미지는 계속 표시됩니다.", "error"), { once: true });
      viewerStage.replaceChildren(viewer);
      currentViewer = viewer;
      viewer.src = new URL(asset.model, manifestUrl).href;
      loadButton.textContent = "3D 미리보기 표시 중";
      setStatus(viewerStatus, `${asset.name} 불러오는 중 (${formatBytes(asset.model_bytes)}).`);
    } catch (error) {
      if (requestedVersion !== selectionVersion || requestedAsset !== currentAsset) return;
      loadButton.disabled = false;
      loadButton.textContent = "다시 불러오기";
      setStatus(viewerStatus, error instanceof Error ? error.message : "3D 뷰어를 불러오지 못했습니다.", "error");
    }
  }

  loadButton.addEventListener("click", loadSelectedModel);
  grid.setAttribute("aria-busy", "true");
  try {
    const response = await fetch("./portfolio.json", { cache: "no-cache", credentials: "same-origin" });
    if (!response.ok) throw new Error(`포트폴리오 정보를 불러오지 못했습니다 (${response.status}).`);
    const manifest = validatePortfolioManifest(await response.json());
    manifestUrl = new URL("./portfolio.json", import.meta.url);
    title.textContent = manifest.title;
    document.title = `${manifest.title} · 3D 포트폴리오`;
    footerTitle.textContent = manifest.title;
    count.textContent = `오브젝트 ${manifest.assets.length}개`;
    grid.replaceChildren();
    if (!manifest.assets.length) {
      grid.append(createElement("p", "empty-state", "아직 공개로 선택한 오브젝트가 없습니다."));
    }
    for (const asset of manifest.assets) {
      const button = createElement("button", "asset-card");
      button.type = "button";
      button.setAttribute("aria-pressed", "false");
      button.setAttribute("aria-label", `${asset.name} 선택`);
      const poster = createElement("img", "asset-poster");
      poster.src = new URL(asset.poster, manifestUrl).href;
      poster.alt = asset.alt || "";
      poster.loading = "lazy";
      poster.decoding = "async";
      const body = createElement("span", "asset-card-body");
      body.append(createElement("span", "asset-card-name", asset.name), createElement("span", "asset-card-hint", "선택해서 자세히 보기"));
      button.append(poster, body);
      button.addEventListener("click", () => selectAsset(asset, button));
      buttons.push(button);
      grid.append(button);
    }
    setStatus(status, manifest.assets.length ? "오브젝트를 선택하면 상세 정보를 볼 수 있습니다." : "공개할 오브젝트를 선택하면 이곳에 표시됩니다.");
  } catch (error) {
    grid.replaceChildren(createElement("p", "empty-state", error instanceof Error ? error.message : "포트폴리오를 읽지 못했습니다."));
    count.textContent = "컬렉션을 사용할 수 없습니다";
    setStatus(status, "이 사이트에 올바른 portfolio.json 파일이 있는지 확인하세요.", "error");
  } finally {
    grid.setAttribute("aria-busy", "false");
  }
}

let manifestUrl;
if (typeof document !== "undefined") loadPortfolio();
