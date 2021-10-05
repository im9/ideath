import React from "react";
import { circleButtonCls, circleButtonActiveCls } from "./CircleButton.css";

type Props = {
  label: String;
  active?: boolean;
  onClick: Function;
};

const CircleButton: React.FC<Props> = ({ label, active = false, onClick }) => {
  const activeCls = active ? circleButtonActiveCls : "";

  return (
    <button
      className={`${circleButtonCls} ${activeCls}`}
      onClick={() => onClick()}
    >
      {label}
    </button>
  );
};

export default CircleButton;
