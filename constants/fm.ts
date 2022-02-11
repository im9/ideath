export const TRACK_LENGTH = 1;

export const DEFAULT_BPM = 120;

export const STEP_LENGTH = 16;

export const NOTES = [...Array(4)].map((_, i) => {
  const index = i + 1;
  return [...Array(2)].map((_, j) => {
    // NOTE: octave 2-9
    const octave = i + index + j;
    return [
      { note: "C", octave },
      { note: "C#", octave }, // d♭
      { note: "D", octave },
      { note: "D#", octave }, // e♭
      { note: "E", octave },
      { note: "F", octave },
      { note: "F#", octave }, // g♭
      { note: "G", octave },
      { note: "G#", octave }, // a♭
      { note: "A", octave },
      { note: "A#", octave }, // b♭
      { note: "B", octave },
    ];
  });
});

export const NOTES_LENGTH = ["1n", "2n", "4n", "8n", "16n", "32n"];

export const MODULATION_TYPES = ["sine", "triangle", "square", "sawtooth"];

export const DEFAULT_PADS_STATES = [...Array(16)].map((_, i) => {
  return {
    volume: 1,
    octave: 2,
    octaveIndex: 2,
    note: "",
    noteLen: "16n",
  };
});

export const DEFAULT_OPTIONS_STATES = {
  frequency: 200,
  harmonicity: 0.2,
  modulationIndex: 3,
  modulationEnvelope: {
    attack: 0.1,
    decay: 0.8,
    sustain: 0.2,
    release: 0.2,
  },
};
