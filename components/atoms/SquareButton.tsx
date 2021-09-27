import React, { useCallback } from "react";
import styles from "./SquareButton.module.scss";

type Props = {
  label: String;
  clickButton: Function;
};

const SquareButton: React.FC<Props> = ({ label, clickButton }) => {
  const handleButtonClick = useCallback(() => {
    clickButton();
  }, [clickButton]);

  return (
    <button className={styles.SquareButton} onClick={handleButtonClick}>
      {label}
    </button>
  );
};

export default SquareButton;
