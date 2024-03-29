export default class Audio {
  public ctx: any;

  constructor() {
    if (!process.browser) return;
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    this.ctx = new AudioContext();
  }

  get() {
    return this.ctx;
  }

  resume() {
    this.ctx.resume();
  }
}
