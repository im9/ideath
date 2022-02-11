import React from "react";
import { NOTES } from "@/constants/fm";
import * as styles from "./NoteKeys.css";

type Props = {
  octaveIndex: number;
  selectedOctave: number; // TODO: 和音に対応
  selectedValues: string[]; // TODO: 和音に対応
  onClick: Function;
};

const NoteKeys: React.FC<Props> = ({
  octaveIndex,
  selectedOctave,
  selectedValues,
  onClick,
}) => {
  const notes = NOTES[octaveIndex];
  /**
   * 白鍵を描画する
   */
  const allWhiteKeys = notes
    ?.flat()
    ?.map(({ note, octave }: { note: string; octave: number }, index) => {
      if (note?.includes("#")) {
        return null;
      }

      const checkedCls =
        selectedValues.includes(note) && selectedOctave === octave
          ? styles.whiteKeyPushed
          : "";
      return (
        <div
          key={index}
          className={styles.whiteKeyBlock}
          onClick={() => onClick(note, octave)}
        >
          <div className={`${styles.whiteKey} ${checkedCls}`} />
        </div>
      );
    });

  /**
   * 黒鍵を描画する
   */
  const allBlackKeys = notes
    ?.flat()
    ?.map(({ note, octave }: { note: string; octave: number }, index) => {
      if (!note?.includes("#")) {
        return null;
      }

      const checkedCls =
        selectedValues.includes(note) && selectedOctave === octave
          ? styles.blackKeyPushed
          : "";
      return (
        <div
          key={index}
          className={styles.blackKeyBlock}
          onClick={() => onClick(note, octave)}
        >
          <div className={`${styles.blackKey} ${checkedCls}`} />
        </div>
      );
    });

  return (
    <>
      <div className={styles.blackkeysArea1}>{allBlackKeys}</div>
      <div className={styles.whitekeysArea}>{allWhiteKeys}</div>
    </>
  );
};

export default NoteKeys;
