import React, { useCallback } from "react";
import { SquareButtonCls } from "./SquareButton.css";

type Props = {
  label: String;
  onClick: Function;
};

const SquareButton: React.FC<Props> = ({ label, onClick }) => {
  const handleButtonClick = useCallback(() => {
    onClick();
  }, [onClick]);

  return (
    <button className={SquareButtonCls} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default SquareButton;
