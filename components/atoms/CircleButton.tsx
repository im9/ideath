import React, { useCallback } from "react";
import styles from "./CircleButton.module.scss";

type Props = {
  label: String;
  clickButton: Function;
};

const CircleButton: React.FC<Props> = ({ label, clickButton }) => {
  const handleButtonClick = useCallback(() => {
    clickButton();
  }, [clickButton]);

  return (
    <button className={styles.CircleButton} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default CircleButton;
