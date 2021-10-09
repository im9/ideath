const MAX_BPM = 300;
const MIN_BPM = 20;

export function master(state: any, action: any) {
  switch (action.type) {
    case "SET_PLAY":
      return {
        ...state,
        master: {
          ...state.master,
          play: action.payload,
        },
      };
    case "SET_BPM":
      return {
        ...state,
        master: {
          ...state.master,
          bpm: action.payload,
        },
      };
    case "INCREMENT_BPM":
      return {
        ...state,
        master: {
          ...state.master,
          bpm:
            state.master.bpm < MAX_BPM
              ? state.master.bpm + 1
              : state.master.bpm,
        },
      };
    case "DECREMENT_BPM":
      return {
        ...state,
        master: {
          ...state.master,
          bpm:
            state.master.bpm > MIN_BPM
              ? state.master.bpm - 1
              : state.master.bpm,
        },
      };
    case "SET_VOLUME":
      return {
        ...state,
        master: {
          ...state.master,
          volume: action.payload,
        },
      };
    case "UPDATE_TRACK_EFFECTS":
      return {
        ...state,
        tracks: {
          ...state.tracks,
          ...action.payload,
        },
      };
    default:
      return state;
  }
}
