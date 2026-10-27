export class HotReloadManager {
  constructor({ context, importFeature, canApply, report, persistState = () => {} }) {
    this.context = context;
    this.importFeature = importFeature;
    this.canApply = canApply;
    this.report = report;
    this.persistState = persistState;
    this.features = new Map();
    this.blockedHashes = new Set();
  }

  async register(definition, hash, restoredState = null) {
    const candidate = await this.importFeature(definition, hash);
    this.#validate(candidate, definition.id);
    const instance = await candidate.init(this.context, restoredState || {});
    this.features.set(definition.id, { definition, hash, candidate, instance });
  }

  async apply(manifest) {
    if (!this.canApply() || !this.canApplyFeatures()) return { status: "deferred", changed: [] };
    const changed = [];
    for (const definition of manifest.modules) {
      const current = this.features.get(definition.id);
      if (current?.hash === definition.hash) continue;
      if (!current) {
        try {
          const candidate = await this.importFeature(definition, definition.hash);
          this.#validate(candidate, definition.id);
          if (!this.canApply() || !this.canApplyFeatures()) return { status: "deferred", changed };
          const instance = await candidate.init(this.context, {});
          this.features.set(definition.id, { definition, hash: definition.hash, candidate, instance });
          changed.push(definition.id);
        } catch (error) {
          this.blockedHashes.add(`${definition.id}:${definition.hash}`);
          this.report(`Could not load feature ${definition.id}; other features remain active. ${error.message}`);
        }
        continue;
      }
      const fingerprint = `${definition.id}:${definition.hash}`;
      if (this.blockedHashes.has(fingerprint)) continue;
      const result = await this.#replace(definition, definition.hash, current);
      if (result) changed.push(definition.id);
    }
    const deferred = manifest.modules.some(definition => this.features.get(definition.id)?.hash !== definition.hash && !this.blockedHashes.has(`${definition.id}:${definition.hash}`));
    if (deferred) return { status: "deferred", changed };
    return { status: changed.length ? "updated" : "unchanged", changed };
  }

  async #replace(definition, hash, current) {
    let next;
    try {
      next = await this.importFeature(definition, hash);
      this.#validate(next, definition.id);
    } catch (error) {
      this.blockedHashes.add(`${definition.id}:${hash}`);
      this.report(`Update failed to load ${definition.id}; current version remains active. ${error.message}`);
      return false;
    }
    if (!this.canApply() || current.instance.canReload?.() === false) return false;
    const state = current.instance.snapshot?.() ?? {};
    this.persistState(definition.id, state);
    current.instance.dispose?.();
    try {
      const instance = await next.init(this.context, state);
      this.features.set(definition.id, { definition, hash, candidate: next, instance });
      this.report(`Updated ${definition.id}; session state restored.`);
      return true;
    } catch (error) {
      try {
        const instance = await current.candidate.init(this.context, state);
        this.features.set(definition.id, { ...current, instance });
      } catch (rollbackError) {
        this.report(`Update failed for ${definition.id}; rollback also failed. Reload the local preview to recover. ${rollbackError.message}`);
        this.blockedHashes.add(`${definition.id}:${hash}`);
        return false;
      }
      this.blockedHashes.add(`${definition.id}:${hash}`);
      this.report(`Update failed for ${definition.id}; previous version restored. ${error.message}`);
      return false;
    }
  }

  snapshotAll() {
    return Object.fromEntries([...this.features].map(([id, feature]) => [id, feature.instance.snapshot?.() ?? {}]));
  }

  canApplyFeatures() {
    return this.canApply() && [...this.features.values()].every(feature => feature.instance.canReload?.() !== false);
  }

  #validate(module, expectedId) {
    if (module.contractVersion !== 1 || module.moduleId !== expectedId || typeof module.init !== "function") {
      throw new Error(`Module contract mismatch for ${expectedId}`);
    }
  }
}
