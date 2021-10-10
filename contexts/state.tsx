import { useReducer, createContext } from "react";
import { master } from "@/reducers/master";
import { getDefaultMatrix } from "@/utils";
import {
  TRACK_LENGTH,
  STEP_LENGTH,
  DEFAULT_BPM,
  DEFAULT_SEQ_SAMPLES,
} from "@/constants/seq";

// initial state
const initialState = {
  master: {
    volume: 0,
    bpm: DEFAULT_BPM,
    play: false,
  },
  tracks: {
    matrix: getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH),
    effects: DEFAULT_SEQ_SAMPLES,
  },
};

// create context
const Context = createContext({});

// combine reducer function
const combineReducers =
  (...reducers: any) =>
  (state: any, action: any) => {
    for (let i = 0; i < reducers.length; i++) {
      state = reducers[i](state, action);
    }
    return state;
  };

// context provider
const Provider = ({ children }: any) => {
  const [state, dispatch] = useReducer(combineReducers(master), initialState);
  const value = { state, dispatch };

  return <Context.Provider value={value}>{children}</Context.Provider>;
};

export { Context, Provider };
