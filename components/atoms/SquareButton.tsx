import React, { useCallback } from "react";
import { SquareButtonCls } from "./SquareButton.css";

type Props = {
  label: String;
  clickButton: Function;
};

const SquareButton: React.FC<Props> = ({ label, clickButton }) => {
  const handleButtonClick = useCallback(() => {
    clickButton();
  }, [clickButton]);

  return (
    <button className={SquareButtonCls} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default SquareButton;
