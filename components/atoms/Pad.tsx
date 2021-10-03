import React, { useCallback } from "react";
import { padCls } from "./Pad.css";

type Props = {
  label?: String;
  clickPad: Function;
};

const Pad: React.FC<Props> = ({ label, clickPad }) => {
  const handlePadClick = useCallback(() => {
    clickPad();
  }, [clickPad]);

  return (
    <button className={padCls} onClick={handlePadClick}>
      <span>{label}</span>
    </button>
  );
};

export default Pad;
