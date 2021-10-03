import React, { useCallback } from "react";
import { circleButtonCls, circleButtonActiveCls } from "./CircleButton.css";

type Props = {
  label: String;
  active?: boolean;
  clickButton: Function;
};

const CircleButton: React.FC<Props> = ({
  label,
  active = false,
  clickButton,
}) => {
  const handleButtonClick = useCallback(() => {
    clickButton();
  }, [clickButton]);
  const activeCls = active ? circleButtonActiveCls : "";

  return (
    <button
      className={`${circleButtonCls} ${activeCls}`}
      onClick={handleButtonClick}
    >
      {label}
    </button>
  );
};

export default CircleButton;
