export default class Mic {
  loaded = false;
  context: AudioContext | undefined;
  analyser: any;
  input: MediaStreamAudioSourceNode | undefined;

  constructor() {
    if (!process.browser) return;

    navigator.mediaDevices
      .getUserMedia({ audio: true, video: false })
      .then((stream) => {
        this.context = new AudioContext();
        this.input = this.context.createMediaStreamSource(stream);
        this.analyser = this.context.createAnalyser();
        this.input.connect(this.analyser);
        this.analyser.connect(this.context.destination);
        this.loaded = true;
      });
  }

  getLevel() {
    if (this.loaded) {
      const result = new Uint8Array(this.analyser.frequencyBinCount);
      this.analyser.getByteFrequencyData(result);
      return result.reduce((a, b) => Math.max(a, b));
    }
    return 0;
  }

  close() {
    this.context?.close();
  }
}
