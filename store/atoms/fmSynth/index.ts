import { atom, selector } from "recoil";
import { DEFAULT_PADS_STATES, DEFAULT_OPTIONS_STATES } from "@/constants/fm";

export const fmSynthOptionsState = atom({
  key: "FmSynthOptions",
  default: {
    volume: 1,
    ...DEFAULT_OPTIONS_STATES,
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
