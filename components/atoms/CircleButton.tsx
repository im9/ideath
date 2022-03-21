import React from "react";
import {
  circleButtonCls,
  circleButtonActiveCls,
  circleButtonSmallCls,
} from "./CircleButton.css";

type Props = {
  label: String;
  active?: boolean;
  small?: boolean;
  onClick: Function;
};

const CircleButton: React.FC<Props> = ({
  label,
  active = false,
  small = false,
  onClick,
}) => {
  const activeCls = active ? circleButtonActiveCls : "";
  const smallCls = small ? circleButtonSmallCls : "";

  return (
    <button
      className={`${circleButtonCls} ${activeCls} ${smallCls}`}
      onClick={() => onClick()}
    >
      {label}
    </button>
  );
};

export default CircleButton;
