export const TRACK_LENGTH = 6;

export const DEFAULT_BPM = 120;

export const STEP_LENGTH = 16;

/**
 * path: ファイルパス
 * d: ディストーション
 * r: リバーブ
 */
export const DEFAULT_SEQ_SAMPLES = [
  { path: "/samples/808bd/BD0000.WAV", d: 0, r: 0, v: -40 }, // bass drum
  { path: "/samples/808sd/SD5025.WAV", d: 0, r: 0, v: -40 }, // snare
  { path: "/samples/808/CH.WAV", d: 0, r: 0, v: -40 }, // close hihat
  { path: "/samples/808oh/OH00.WAV", d: 0, r: 0, v: -40 }, // open hihat
  { path: "/samples/808/CP.WAV", d: 0, r: 0, v: -40 }, // clap
  { path: "/samples/808/CB.WAV", d: 0, r: 0, v: -40 }, // cowbell
];

export const TRACK_LABELS = [
  "KICK",
  "SNARE",
  "CLOSE HIHAT",
  "OPEN HIHAT",
  "CLAP",
  "COWBELL",
  "MASTER",
];

// TODO: 実装
export const MODE = {
  DEFAULT: 1,
  MIDI: 2,
};
