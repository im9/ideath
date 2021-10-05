import React from "react";
import { knobCls } from "./Knob.css";

type Props = {
  value?: String;
  onClick: Function;
};

const Knob: React.FC<Props> = ({ value, onClick }) => {
  return (
    <input
      type="range"
      className={knobCls}
      onClick={() => {
        onClick();
      }}
      data-diameter={value}
    />
  );
};

export default Knob;
