import React from "react";
import IconButton from "@/components/atoms/IconButton";
import { qwertyCls, keyCls } from "./Qwerty.css";
import { QWERTY_KEYS } from "@/constants/org";

type Props = {
  pushedList?: any; // FIXME: Object
};

const Qwerty: React.FC<Props> = ({ pushedList }) => {
  /**
   * 各キー配列を描画する
   */
  const keys = QWERTY_KEYS.map((keys, index) => {
    return (
      <div key={index} className={keyCls}>
        {keys.map((key, childIndex) => (
          <IconButton
            key={childIndex}
            icon={String(key).toLocaleUpperCase()}
            pushed={!!pushedList[key]}
          />
        ))}
      </div>
    );
  });

  return <div className={qwertyCls}>{keys}</div>;
};

export default Qwerty;
