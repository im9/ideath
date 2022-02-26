import React, { useCallback } from "react";
import {
  SquareButtonCls,
  SquareButtonSmallCls,
  SquareButtonPushedCls,
} from "./SquareButton.css";

type Props = {
  label: String;
  small?: Boolean;
  pushed?: Boolean;
  onClick: Function;
};

const SquareButton: React.FC<Props> = ({ label, small, pushed, onClick }) => {
  const handleButtonClick = useCallback(() => {
    onClick();
  }, [onClick]);

  const btnCls = pushed ? SquareButtonPushedCls : SquareButtonCls;
  const smallCls = small ? SquareButtonSmallCls : {};

  return (
    <button className={`${btnCls} ${smallCls}`} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default SquareButton;
