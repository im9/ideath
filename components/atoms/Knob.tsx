import React from "react";
import { knobCls } from "./Knob.css";

type Props = {
  value?: String;
  onClick: Function;
};

const Knob: React.FC<Props> = ({ value, onClick }) => {
  return (
    <button
      type="button"
      className={knobCls}
      onClick={() => onClick()}
    ></button>
  );
};

export default Knob;
