import type { NextPage } from "next";
import Head from "next/head";
import { useRef, useState, useEffect, useCallback, useMemo } from "react";
import * as Tone from "tone";

import PlayButton from "@/components/atoms/PlayButton";
import CircleButton from "@/components/atoms/CircleButton";
import SquareButton from "@/components/atoms/SquareButton";
import Knob from "@/components/atoms/Knob";
import StepPad from "@/components/atoms/StepPad";
import Digits from "@/components/molecules/Digits";
import { getDefaultMatrix, getTempo, getTonePlayer } from "@/utils";
import {
  MODE,
  TRACK_LENGTH,
  STEP_LENGTH,
  DEFAULT_BPM,
  DEFAULT_SEQ_SAMPLES,
  TRACK_LABELS,
} from "@/constants/seq";
import {
  containerCls,
  mainCls,
  titleCls,
  controlsCls,
  controlsFuncCls,
  settingsCls,
  settingsTrackCls,
  settingsBpmCls,
  settingsTrackDisplayCls,
  settingsTrackDisplayDlCls,
  settingsTrackDisplayDlDdCls,
  settingsTrackButtonAreaCls,
  padsCls,
  padsWrapperCls,
} from "./seq.css";

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
    return DEFAULT_SEQ_SAMPLES.map(({ path, d, r }) =>
      getTonePlayer(path, d, r)
    );
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
   * 選択中のトラックのリバーブを設定する
   */
  const handleTrackReverbKnobCtl = useCallback(() => {
    if (samples[selectedTrack] && Tone.loaded()) {
      console.log(DEFAULT_SEQ_SAMPLES[selectedTrack], samples[selectedTrack]);
      const { path, r, d } = DEFAULT_SEQ_SAMPLES[selectedTrack];
      samples[selectedTrack] = getTonePlayer(path, d, 1);
    }
    // matrixRef.current.map((col: number[], index: number) => {
    //   if (col[stepRef.current] && samples[index] && Tone.loaded()) {
    //     samples[index].start();
    //   }
    // });
  }, [samples, selectedTrack]);

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
      if (col[stepRef.current] && samples[index]) {
        Tone.loaded().then(() => samples[index]?.start());
      }
    });
  }, [play, step, samples]);

  /**
   * ディスプレイを描画する
   */
  const display = useMemo(() => {
    const trackNum = selectedTrack ? selectedTrack + 1 : 1;
    const current = TRACK_LABELS[trackNum - 1];
    const name = trackNum ? DEFAULT_SEQ_SAMPLES[trackNum - 1]?.path : "";
    return (
      <dl className={settingsTrackDisplayDlCls}>
        <dt>{trackNum}</dt>
        <dd className={settingsTrackDisplayDlDdCls}>
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
        onClick={() => {
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
          onClick={() => {
            handlePadClick(row, col);
          }}
        ></StepPad>
      );
    });

    return (
      <div key={row} className={padsCls}>
        {pad}
      </div>
    );
  });

  return (
    <div className={containerCls}>
      <Head>
        <title>Seq</title>
        <meta name="description" content="seq" />
        <link rel="icon" href="/favicon.ico" />
      </Head>
      <main className={mainCls}>
        <h1 className={titleCls}>Seq</h1>
        <div className={settingsCls}>
          <div className={settingsTrackCls}>
            <div className={settingsTrackDisplayCls}>{display}</div>
          </div>
          <div className={settingsBpmCls}>
            <CircleButton
              label="＋"
              onClick={() => {
                handleBpmBtnClick(true);
              }}
            />
            <CircleButton label="−" onClick={handleBpmBtnClick} />
          </div>
          <div>
            <Digits bpm={bpm} />
          </div>
        </div>
        <div className={settingsTrackButtonAreaCls}>{trackBtns}</div>
        <div className={controlsCls}>
          <PlayButton pushed={play} onClick={handlePlayBtnClick} />
          <div className={controlsFuncCls}>
            {/* TODO: モード切り替えの実装 */}
            {/* <SquareButton label="FUNC" onClick={handleFuncBtnClick} /> */}
            <Knob onClick={handleTrackReverbKnobCtl} />
          </div>
          <div>
            <SquareButton label="RESET" onClick={handleResetBtnClick} />
          </div>
        </div>
        <div className={padsWrapperCls}>{pads}</div>
      </main>
    </div>
  );
};

export default Seq;
