class MyProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return [
      {
        name: "mode",
        defaultValue: 0,
      },
      {
        name: "freq",
        defaultValue: 440.0,
      },
      {
        name: "gain",
        defaultValue: 1,
      },
      {
        name: "cutoff",
        defaultValue: 1000,
      },
      {
        name: "q",
        defaultValue: 20,
      },
      {
        name: "decay",
        defaultValue: 0.2,
      },
      {
        name: "amount",
        defaultValue: 0.2,
      },
    ];
  }

  constructor() {
    super();

    this.port.onmessage = (e) => {
      if (e.data.type === "loadWasm") {
        WebAssembly.instantiate(e.data.data).then((w) => {
          this._wasm = w.instance;
          this._size = 128;
          this._inPtr = this._wasm.exports.alloc(this._size);
          this._outPtr = this._wasm.exports.alloc(this._size);
          this._inBuf = new Float32Array(
            this._wasm.exports.memory.buffer,
            this._inPtr,
            this._size
          );
          this._outBuf = new Float32Array(
            this._wasm.exports.memory.buffer,
            this._outPtr,
            this._size
          );
        });
      } else if (e.data.type === "trigger") {
        this._wasm.exports.trigger();
      }
    };
  }

  process(inputs, outputs, parameters) {
    if (!this._wasm) {
      return true;
    }
    this._wasm.exports.set_mode(parameters.mode[0]);
    this._wasm.exports.set_frequency(parameters.freq[0]);
    this._wasm.exports.set_gain(parameters.gain[0]);
    this._wasm.exports.set_cutoff(parameters.cutoff[0]);
    this._wasm.exports.set_q(parameters.q[0]);
    this._wasm.exports.set_decay(parameters.decay[0]);
    this._wasm.exports.set_amount(parameters.amount[0]);

    let output = outputs[0];
    for (let channel = 0; channel < output.length; ++channel) {
      let outputChannel = output[channel];
      this._wasm.exports.process(this._outPtr, this._size);
      outputChannel.set(this._outBuf);
    }

    return true;
  }
}

registerProcessor("my-processor", MyProcessor);
