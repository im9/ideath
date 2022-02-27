import React from "react";
import * as styles from "./BaseFooter.css";

/**
 * フッターを描画する
 */
const BaseFooter: React.FC = () => {
  return <footer className={styles.footerWrapper}>&copy;im9</footer>;
};

export default BaseFooter;
