export class Preferences {
  constructor(storage) {
    try { this.storage = storage ?? globalThis.localStorage; }
    catch { this.storage = null; }
    this.memory = new Map();
    this.persistent = Boolean(storage);
  }

  get(key, fallback = null) {
    try {
      const value = this.storage?.getItem(key);
      if (value !== null && value !== undefined) {
        try { return JSON.parse(value); }
        catch { return value; } // migrate the preview's original plain-string preference format.
      }
    } catch { this.persistent = false; }
    return this.memory.has(key) ? this.memory.get(key) : fallback;
  }

  set(key, value) {
    this.memory.set(key, value);
    try { this.storage?.setItem(key, JSON.stringify(value)); }
    catch { this.persistent = false; }
    return this.persistent;
  }
}
