export const QWERTY_KEYS: string[][] = [
  ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
  ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
  ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
  ["z", "x", "c", "v", "b", "n", "m"],
];

// キーボード配列のイベント管理の初期値
export const DEFAULT_QWERTY_VALUES = (() => {
  let keys = {};
  QWERTY_KEYS.flat().forEach((key) => {
    keys = {
      ...keys,
      [key]: false,
    };
  });
  return keys;
})();
