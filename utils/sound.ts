import Reverb from "soundbank-reverb";

export default class Sound {
  audioContext: AudioContext;
  oscillator: OscillatorNode | undefined;
  gain: GainNode | undefined;
  delay: DelayNode | undefined;
  dry: GainNode | undefined;
  wet: GainNode | undefined;
  feedback: GainNode | undefined;
  reverb: any;

  constructor(audioContext: AudioContext) {
    this.audioContext = audioContext;
  }

  init(waveType: any) {
    this.oscillator = this.audioContext.createOscillator();
    this.gain = this.audioContext.createGain();
    // this.delay = this.audioContext.createDelay(2);
    // this.dry = this.audioContext.createGain(); // for gain of original sound
    // this.wet = this.audioContext.createGain(); // for gain of effect (Delay) sound
    // this.feedback = this.audioContext.createGain(); // for feedback

    // reverb
    this.reverb = Reverb(this.audioContext);
    this.reverb.connect(this.audioContext.destination);
    this.oscillator.connect(this.reverb);
    this.reverb.time = 1; //seconds
    this.reverb.wet.value = 0.1;
    this.reverb.dry.value = 0.1;

    this.reverb.filterType = "lowpass";
    this.reverb.cutoff.value = 4000; //Hz

    // setInterval(() => {
    //   const source = this.audioContext.createOscillator();
    //   source.type = "sawtooth";
    //   source.connect(this.reverb);
    //   source.start();
    //   source.stop(this.audioContext.currentTime + 0.5);
    // }, 2000);

    // Set parameters
    // this.dry.gain.value = 0.1;
    // this.wet.gain.value = 0.1;
    // this.feedback.gain.value = 0.1;

    // this.oscillator.connect(this.gain);
    // this.oscillator.connect(this.delay);

    // for feedback
    // this.oscillator.connect(this.dry);

    // this.delay.connect(this.wet);
    // this.wet.connect(this.audioContext.destination);
    // this.dry.connect(this.audioContext.destination);

    // this.delay.connect(this.feedback);
    // this.feedback.connect(this.delay);

    this.gain.connect(this.audioContext.destination);
    this.oscillator.type = waveType;
  }

  play(
    freqValue: number,
    waveType: string,
    volume: number,
    startTime: number | undefined,
    endTime: number | undefined
  ) {
    this.init(waveType);

    if (this.oscillator) this.oscillator.frequency.value = freqValue;

    this.gain?.gain.setValueAtTime(volume, this.audioContext.currentTime);

    this.oscillator?.start(startTime);

    if (endTime !== undefined) {
      this.stop(endTime);
    }
  }

  stop(time: number) {
    this.gain?.gain.exponentialRampToValueAtTime(0.001, time);
    this.oscillator?.stop(time + 1);
  }
}
function audioBase64(audioBase64: any, callback: any) {
  throw new Error("Function not implemented.");
}
