import React, { useCallback } from "react";
// import styles from "./CircleButton.module.scss";
import { circleButtonCls } from "./CircleButton.css";

type Props = {
  label: String;
  clickButton: Function;
};

const CircleButton: React.FC<Props> = ({ label, clickButton }) => {
  const handleButtonClick = useCallback(() => {
    clickButton();
  }, [clickButton]);

  return (
    <button className={circleButtonCls} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default CircleButton;
