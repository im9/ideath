export const TRACK_LENGTH = 6;

export const STEP_LENGTH = 16;

export const DEFAULT_BPM = 120;

export const DEFAULT_SEQ_SAMPLES = [
  { path: "/samples/808bd/BD0000.WAV", d: null, r: null }, // bass drum
  { path: "/samples/808sd/SD5025.WAV", d: null, r: null }, // snare
  { path: "/samples/808/CH.WAV", d: null, r: null }, // close hihat
  { path: "/samples/808oh/OH00.WAV", d: null, r: null }, // open hihat
  { path: "/samples/808/CP.WAV", d: null, r: null }, // clap
  { path: "/samples/808/CB.WAV", d: null, r: null }, // cowbell
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
