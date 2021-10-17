import React, { useCallback } from "react";
import { SquareButtonCls, SquareButtonSmallCls } from "./SquareButton.css";

type Props = {
  label: String;
  small?: Boolean;
  onClick: Function;
};

const SquareButton: React.FC<Props> = ({ label, small, onClick }) => {
  const handleButtonClick = useCallback(() => {
    onClick();
  }, [onClick]);

  const smallCls = small ? SquareButtonSmallCls : {};

  return (
    <button
      className={`${SquareButtonCls} ${smallCls}`}
      onClick={handleButtonClick}
    >
      {label}
    </button>
  );
};

export default SquareButton;
