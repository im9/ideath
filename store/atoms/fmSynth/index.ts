import { atom, selector } from "recoil";

export const DEFAULT_PADS_STATES = [...Array(16)].map((_, i) => {
  return {
    volume: 1,
    octave: 2,
    octaveIndex: 2,
    note: "",
    noteLen: "16n",
  };
});

export const fmSynthOptionsState = atom({
  key: "FmSynthOptions",
  default: {
    volume: 1,
    frequency: 200,
    harmonicity: 0.2,
    modulationIndex: 3,
    modulationEnvelope: {
      attack: 0.1,
      decay: 0.8,
      sustain: 0.2,
      release: 0.2,
    },
  },
});

export const fmSynthStepPadsState = atom({
  key: "FmSynthStepPadsState",
  default: DEFAULT_PADS_STATES,
});

// export const fmSynthStepPadsSelector = selector({
//   key: "FmSynthStepPadsSelector",
//   get: ({ get }) => {},
// });
