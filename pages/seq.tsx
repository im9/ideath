import type { NextPage } from "next";
import { useRef, useState, useEffect, useCallback, useMemo } from "react";
import Head from "next/head";
import * as Tone from "tone";

import PlayButton from "@/components/atoms/PlayButton";
import CircleButton from "@/components/atoms/CircleButton";
import Digits from "@/components/molecules/Digits";
import styles from "@/styles/Seq.module.scss";

const Seq: NextPage = () => {
  const [play, setPlay] = useState(false);
  const [matrix, setMatrix] = useState([
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
  ]);
  const [bpm, setBpm] = useState(120);
  const [step, setStep] = useState(0);
  const stepRef = useRef(0);
  const intervalRef: any = useRef(null);
  const matrixRef = useRef(matrix);
  const tempoRef = useRef((60 / bpm) * 500);

  // kick を初期化
  const kick = useMemo(() => {
    if (!process.browser) return;
    const kick = new Tone.Player("/samples/808bd/BD0000.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return kick.connect(distortion);
  }, []);

  // hihat を初期化
  const hihat = useMemo(() => {
    if (!process.browser) return;
    const hihat = new Tone.Player("/samples/808/CH.WAV").toDestination();
    const hihatDistortion = new Tone.Distortion(0.4).toDestination();
    return hihat.connect(hihatDistortion);
  }, []);

  // snare を初期化
  const snare = useMemo(() => {
    if (!process.browser) return;
    const snare = new Tone.Player("/samples/808sd/SD5025.WAV").toDestination();
    const snareDistortion = new Tone.Distortion(0.4).toDestination();
    return snare.connect(snareDistortion);
  }, []);

  // clap を初期化
  const clap = useMemo(() => {
    if (!process.browser) return;
    return new Tone.Player("/samples/808/CP.WAV").toDestination();
  }, []);

  useEffect(() => {
    tempoRef.current = (60 / bpm) * 500;

    Tone.Transport.start();
    Tone.Transport.bpm.rampTo(bpm, 5);
  }, [bpm]);

  useEffect(() => {
    stepRef.current = step;

    /**
     * 各トラックで押下されているステップの音を再生する
     */
    if (matrixRef.current[0][stepRef.current]) {
      Tone.loaded().then(() => kick?.start());
    }

    if (matrixRef.current[1][stepRef.current]) {
      Tone.loaded().then(() => hihat?.start());
    }

    if (matrixRef.current[2][stepRef.current]) {
      Tone.loaded().then(() => snare?.start());
    }

    if (matrixRef.current[3][stepRef.current]) {
      Tone.loaded().then(() => clap?.start());
    }
  }, [step, kick, hihat, snare, clap]);

  useEffect(() => {
    matrixRef.current = matrix;
  }, [matrix]);

  /**
   * 再生／停止ボタンのクリックイベントをハンドルする
   */
  const handlePlayBtnClick = useCallback(
    async (isStart) => {
      setPlay(isStart);

      // Audio Context 開始
      await Tone.start();

      if (isStart) {
        // 再生ボタンが押下されたらSTEPを更新する
        const len = matrix[0].length - 1;
        intervalRef.current = setInterval(() => {
          setStep((step) => (step < len ? ++step : 0));
        }, tempoRef.current);
      } else {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    },
    [matrix]
  );

  /**
   * 各パッドのクリックイベントをハンドルする
   */
  const handlePadClick = useCallback(
    (index, key) => {
      matrix[index][key] = Number(!matrix[index][key]);
      setMatrix(JSON.parse(JSON.stringify(matrix)));
    },
    [matrix]
  );

  /**
   * BPM を加算/減算する
   */
  const handleBpmBtnClick = useCallback((add = false) => {
    setBpm((bpm) => {
      if (add && bpm < 300) {
        return ++bpm;
      } else if (!add && bpm > 20) {
        return --bpm;
      }
      return bpm;
    });
  }, []);

  /**
   * 各パッドを描画する
   */
  const pads = matrix.map((track, row) => {
    const pad = track.map((_, col) => {
      let isPushed = Number(matrix[row][col]);
      let isCurrent = col === step;
      return (
        <div
          key={`${row}${col}${isPushed}`}
          className={`
            ${styles.Pad}
            ${isPushed ? styles["Pad--pushed"] : ""}
            ${isCurrent ? styles["Pad--current"] : ""}
          `}
          onClick={() => {
            handlePadClick(row, col);
          }}
        ></div>
      );
    });

    return (
      <div key={row} className={styles.Pads}>
        {pad}
      </div>
    );
  });

  return (
    <div className={styles.container}>
      <Head>
        <title>Seq</title>
        <meta name="description" content="seq" />
        <link rel="icon" href="/favicon.ico" />
      </Head>
      <main className={styles.main}>
        <div className={styles.Settings}>
          <div className={styles.SettingsBpm}>
            <CircleButton
              label="＋"
              clickButton={() => {
                handleBpmBtnClick(true);
              }}
            />
            <CircleButton label="−" clickButton={handleBpmBtnClick} />
          </div>
          <div>
            <Digits bpm={bpm} />
          </div>
        </div>
        <div className={styles.Controls}>
          <PlayButton pushed={play} clickButton={handlePlayBtnClick} />
        </div>
        <div className={styles.PadsWrapper}>{pads}</div>
      </main>
    </div>
  );
};

export default Seq;
