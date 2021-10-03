import type { NextPage } from "next";
import Head from "next/head";
import { useRef, useState, useEffect, useCallback, useMemo } from "react";
import * as Tone from "tone";

import PlayButton from "@/components/atoms/PlayButton";
import CircleButton from "@/components/atoms/CircleButton";
import SquareButton from "@/components/atoms/SquareButton";
import StepPad from "@/components/atoms/StepPad";
import Digits from "@/components/molecules/Digits";
import { getDefaultMatrix, getTempo, getTonePlayer } from "@/utils";
import styles from "@/styles/Seq.module.scss";

const TRACK_LENGTH = 6;
const STEP_LENGTH = 16;
const DEFAULT_BPM = 120;
const DEFAULT_SAMPLES = [
  { path: "/samples/808bd/BD0000.WAV", d: null, r: null }, // bass drum
  { path: "/samples/808sd/SD5025.WAV", d: null, r: null }, // snare
  { path: "/samples/808/CH.WAV", d: null, r: null }, // close hihat
  { path: "/samples/808oh/OH00.WAV", d: null, r: null }, // open hihat
  { path: "/samples/808/CP.WAV", d: null, r: null }, // clap
  { path: "/samples/808/CB.WAV", d: null, r: null }, // cowbell
];
const TRACK_LABELS = [
  "KICK",
  "SNARE",
  "CLOSE HIHAT",
  "OPEN HIHAT",
  "CLAP",
  "COWBELL",
  "MASTER",
];
// TODO: 実装
const MODE = {
  DEFAULT: 1,
  MIDI: 2,
};

/**
 * リズムマシン
 */
const Seq: NextPage = () => {
  // セッティング系
  const [play, setPlay] = useState(false);
  const [mode, setMode] = useState(MODE.DEFAULT);
  const [selectedTrack, setSelectedTrack] = useState(TRACK_LABELS.length - 1);

  // リズムマシン系
  const [matrix, setMatrix] = useState(
    getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH)
  );
  const [bpm, setBpm] = useState(DEFAULT_BPM);
  const [processing, setProcessing] = useState(false);
  const [step, setStep] = useState(0);
  const stepRef = useRef(0);
  const intervalRef: any = useRef(null);
  const matrixRef = useRef(matrix);
  const tempoRef = useRef(getTempo(bpm));

  // 各トラックのサンプルを初期化
  const samples: any = useMemo(() => {
    if (!process.browser) return { start: () => {} };
    return DEFAULT_SAMPLES.map(({ path, d, r }) => getTonePlayer(path, d, r));
  }, []);

  /**
   * 再生／停止ボタンのクリックイベントをハンドルする
   */
  const handlePlayBtnClick = useCallback(async (isStart) => {
    setPlay(isStart);

    // Audio Context 開始
    await Tone.start();

    if (isStart) {
      // 再生ボタンが押下されたらSTEPを更新する
      intervalRef.current = setInterval(() => {
        setStep((step) => (step < STEP_LENGTH - 1 ? ++step : 0));
      }, tempoRef.current);
    } else {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
  }, []);

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
   * TODO: ファンクションボタンのクリックをハンドルする
   */
  const handleFuncBtnClick = useCallback(() => {
    setMode(() => (mode === MODE.DEFAULT ? MODE.MIDI : MODE.DEFAULT));
  }, [mode]);

  /**
   * トラック選択ボタンのクリックをハンドルする
   */
  const handleTrackBtnClick = useCallback((value) => {
    setSelectedTrack(value);
  }, []);

  /**
   * リセットボタンのクリックをハンドルする
   */
  const handleResetBtnClick = useCallback(() => {
    // ステップを初期化
    setStep(0);
    stepRef.current = 0;

    // 全トラックの押下状態を初期化
    setMatrix(getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH));
    matrixRef.current = matrix;

    // BPM を初期化
    setBpm(DEFAULT_BPM);
    tempoRef.current = getTempo(bpm);

    // タイマーを初期化
    setPlay(false);
    clearInterval(intervalRef.current);

    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /**
   * BPM を加算/減算する
   */
  const handleBpmBtnClick = useCallback(
    (add = false) => {
      setBpm((bpm) => {
        if (add && bpm < 300) {
          return ++bpm;
        } else if (!add && bpm > 20) {
          return --bpm;
        }
        return bpm;
      });
      if (play && !processing) {
        // 連打対策
        setProcessing(true);

        setTimeout(() => {
          setProcessing(false);
        }, 500);

        // 算出した BPM を基準にタイマーを初期化する
        initTimer();
      }
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [play, processing]
  );

  /**
   * タイマーを初期化する
   */
  const initTimer = useCallback(() => {
    clearInterval(intervalRef.current);

    intervalRef.current = setInterval(() => {
      setStep((step) => (step < STEP_LENGTH - 1 ? ++step : 0));
    }, tempoRef.current);
  }, []);

  /**
   * BPM をもとにテンポを設定する
   */
  useEffect(() => {
    tempoRef.current = getTempo(bpm);

    Tone.Transport.start();

    // FIXME: Tone のシーケンサーを利用する場合は設定する
    // Tone.Transport.bpm.rampTo(bpm, 5);
  }, [bpm]);

  /**
   * 参照用の行列の状態を設定する
   */
  useEffect(() => {
    matrixRef.current = matrix;
  }, [matrix]);

  /**
   * 各トラックで押下されているステップの音を再生する
   */
  useEffect(() => {
    stepRef.current = step;

    matrixRef.current.map((col: number[], index: number) => {
      if (col[stepRef.current] && samples[index] && Tone.loaded()) {
        samples[index].start();
      }
    });
  }, [play, step, samples]);

  /**
   * ディスプレイを描画する
   */
  const display = useMemo(() => {
    const trackNum = selectedTrack ? selectedTrack + 1 : 1;
    const current = TRACK_LABELS[trackNum - 1];
    const name = trackNum ? DEFAULT_SAMPLES[trackNum - 1]?.path : "";
    return (
      <dl>
        <dt>{trackNum}</dt>
        <dd>
          <div>{current}</div>
          <div>{name}</div>
        </dd>
      </dl>
    );
  }, [selectedTrack]);

  /**
   * 各トラックの選択ボタンを描画する
   */
  const trackBtns = matrix.concat(Array([])).map((_, row) => {
    const label = row !== 6 ? String(row + 1) : "M";
    return (
      <CircleButton
        key={row}
        label={label}
        active={selectedTrack === row}
        clickButton={() => {
          handleTrackBtnClick(row);
        }}
      />
    );
  });

  /**
   * 各パッドを描画する
   */
  const pads = matrix.map((track, row) => {
    const pad = track.map((_, col) => {
      const isPushed = !!matrix[row][col];
      const isCurrent = col === step && play;
      const isActive = selectedTrack === row;
      return (
        <StepPad
          key={`${row}${col}${isPushed}`}
          pushed={isPushed}
          current={isCurrent}
          active={isActive}
          row={row}
          col={col}
          clickButton={() => {
            handlePadClick(row, col);
          }}
        ></StepPad>
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
        <h1>Seq</h1>
        <div className={styles.Settings}>
          <div className={styles.SettingsTrack}>
            <div className={styles.SettingsTrackDisplay}>{display}</div>
          </div>
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
        <div className={styles.SettingsTrackButtonArea}>{trackBtns}</div>
        <div className={styles.Controls}>
          <PlayButton pushed={play} clickButton={handlePlayBtnClick} />
          <div className={styles.Func}>
            {/* TODO: モード切り替えの実装 */}
            {/* <SquareButton label="FUNC" clickButton={handleFuncBtnClick} /> */}
          </div>
          <div>
            <SquareButton label="RESET" clickButton={handleResetBtnClick} />
          </div>
        </div>
        <div className={styles.PadsWrapper}>{pads}</div>
      </main>
    </div>
  );
};

export default Seq;
