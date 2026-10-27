export class ActivityTracker {
  #nextId = 0;
  #active = new Map();

  begin(label) {
    const id = ++this.#nextId;
    this.#active.set(id, label);
    let finished = false;
    return () => {
      if (finished) return;
      finished = true;
      this.#active.delete(id);
    };
  }

  get isIdle() { return this.#active.size === 0; }
  get activeLabels() { return [...this.#active.values()]; }
}
