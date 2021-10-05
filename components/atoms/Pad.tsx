import React, { useCallback } from "react";
import { padCls } from "./Pad.css";

type Props = {
  label?: String;
  onClick: Function;
};

const Pad: React.FC<Props> = ({ label, onClick }) => {
  const handlePadClick = useCallback(() => {
    onClick();
  }, [onClick]);

  return (
    <button className={padCls} onClick={handlePadClick}>
      <span>{label}</span>
    </button>
  );
};

export default Pad;
