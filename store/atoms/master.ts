import { atom, selector } from "recoil";
import { DEFAULT_QWERTY_VALUES } from "@/constants/org";

export const qwertyMatrixState = atom({
  key: "QwertyMatrix",
  default: DEFAULT_QWERTY_VALUES,
});

export const currentQwertyMatrixSelector = selector({
  key: "CurrentQwertyMatrix",
  get: ({ get }) => {
    return get(qwertyMatrixState);
  },
});
