import type { NextPage } from "next";
import Head from "next/head";
import {
  useRef,
  useState,
  useContext,
  useEffect,
  useCallback,
  useMemo,
} from "react";
import * as Tone from "tone";
import MainLayout from "@/components/layouts/MainLayout";
import PlayButton from "@/components/atoms/PlayButton";
import CircleButton from "@/components/atoms/CircleButton";
import SquareButton from "@/components/atoms/SquareButton";
import Knob from "@/components/atoms/Knob";
import StepPad from "@/components/atoms/StepPad";
import Digits from "@/components/molecules/Digits";
import { Context } from "@/contexts/state";
import { useModal } from "hooks/usePanel";
import { getDefaultMatrix, getTempo, getTonePlayer, percent } from "@/utils";
import {
  TRACK_LENGTH,
  STEP_LENGTH,
  DEFAULT_BPM,
  DEFAULT_SEQ_SAMPLES,
  TRACK_LABELS,
} from "@/constants/seq";
import * as styles from "@/styles/seq.css";

/**
 * リズムマシン
 */
const Seq: NextPage = () => {
  // context
  const {
    state: { master, tracks },
    dispatch,
  }: any = useContext(Context);

  // セッティング系
  const [selectedTrack, setSelectedTrack] = useState(TRACK_LABELS.length - 1);
  // FIXME: 各トラックの楽器を選択可能にする
  const [selectedSamples, setSelectedSamples] = useState(DEFAULT_SEQ_SAMPLES);

  // リズムマシン系
  const [matrix, setMatrix] = useState(
    getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH)
  );
  const [processing, setProcessing] = useState(false);
  const [step, setStep] = useState(0);
  const stepRef = useRef(0);
  const intervalRef: any = useRef(null);
  const matrixRef = useRef(matrix);
  const tempoRef = useRef(getTempo(master.bpm));

  const [Modal, openModal, closeModal, isOpenModal] = useModal("root", {
    preventScroll: true,
  });

  // 各トラックのサンプルを初期化
  const samples: any = useMemo(() => {
    if (!process.browser) return { start: () => {} };
    return selectedSamples.map(({ path, d, r, v }) =>
      getTonePlayer(path, d, r, v)
    );
  }, [selectedSamples]);

  /**
   * 再生／停止ボタンのクリックイベントをハンドルする
   */
  const handlePlayBtnClick = useCallback(
    async (isStart) => {
      dispatch({
        type: "SET_PLAY",
        payload: isStart,
      });

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
    },
    [dispatch]
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
   * トラック選択ボタンのクリックをハンドルする
   */
  const handleTrackBtnClick = useCallback((value) => {
    setSelectedTrack(value);
  }, []);

  /**
   * マスターヴォリュームの値を更新する
   */
  const handleMasterVolumeKnobCtl = useCallback(
    (value) => {
      selectedSamples.forEach((sample, index) => {
        const { path, d, r } = sample;
        samples[index] = getTonePlayer(path, d, r, value * 10);
      });

      dispatch({
        type: "SET_VOLUME",
        payload: value,
      });
    },
    [dispatch, samples, selectedSamples]
  );

  /**
   * 選択中のトラックのディストーションの値を更新する
   */
  const handleTrackDistortionKnobCtl = useCallback(
    (value) => {
      if (samples[selectedTrack] && Tone.loaded()) {
        setSelectedSamples(() => {
          selectedSamples[selectedTrack].d = value;
          return selectedSamples;
        });
        dispatch({
          type: "UPDATE_TRACK_EFFECTS",
          payload: selectedSamples,
        });
        // TODO debounce
      }
    },
    [samples, selectedTrack, dispatch, selectedSamples]
  );

  /**
   * 選択中のトラックのリバーブの値を更新する
   */
  const handleTrackReverbKnobCtl = useCallback(
    (value) => {
      if (samples[selectedTrack] && Tone.loaded()) {
        setSelectedSamples(() => {
          selectedSamples[selectedTrack].r = value;
          return selectedSamples;
        });
        dispatch({
          type: "UPDATE_TRACK_EFFECTS",
          payload: selectedSamples,
        });
        // TODO debounce
      }
    },
    [samples, selectedTrack, dispatch, selectedSamples]
  );

  /**
   * 選択中のトラックのエフェクトを設定する
   */
  const handleTrackEffectKnobCtl = useCallback(() => {
    if (samples[selectedTrack] && Tone.loaded()) {
      const { path, d, r, v } = selectedSamples[selectedTrack];
      samples[selectedTrack] = getTonePlayer(path, d, r, v);
    }
  }, [samples, selectedSamples, selectedTrack]);

  /**
   * リセットボタンのクリックをハンドルする
   */
  const handleResetBtnClick = useCallback(() => {
    if (isOpenModal) return;
    openModal();

    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /**
   * リセットボタンのクリックをハンドルする
   */
  const handleResetOKBtnClick = useCallback(() => {
    // ステップを初期化
    setStep(0);
    stepRef.current = 0;

    // 全トラックの押下状態を初期化
    setMatrix(getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH));
    matrixRef.current = matrix;

    // BPM を初期化
    dispatch({
      type: "SET_BPM",
      payload: DEFAULT_BPM,
    });
    tempoRef.current = getTempo(master.bpm);

    // タイマーを初期化
    dispatch({
      type: "SET_PLAY",
      payload: false,
    });
    clearInterval(intervalRef.current);

    // モーダルを閉じる
    closeModal();

    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /**
   * BPM を加算/減算する
   */
  const handleBpmBtnClick = useCallback(
    (add = false) => {
      dispatch({ type: add ? "INCREMENT_BPM" : "DECREMENT_BPM" });

      if (master.play && !processing) {
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
    [master, processing]
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
    tempoRef.current = getTempo(master.bpm);

    Tone.Transport.start();

    // FIXME: Tone のシーケンサーを利用する場合は設定する
    // Tone.Transport.bpm.rampTo(bpm, 5);
  }, [master.bpm]);

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
  }, [master.play, step, samples]);

  /**
   * ディスプレイを描画する
   */
  const trackNum = useMemo(
    () => (selectedTrack ? selectedTrack + 1 : 1),
    [selectedTrack]
  );
  const current = useMemo(() => TRACK_LABELS[trackNum - 1], [trackNum]);
  const name = useMemo(
    () => selectedSamples[trackNum - 1]?.path || "",
    [selectedSamples, trackNum]
  );
  const display = !name ? (
    <dl>
      <dt>0</dt>
      <dd>
        <div>Master</div>
        <div className={styles.settingsTrackDisplayEffectAreaCls}>
          <div>Volume: {percent(master.volume)}</div>
        </div>
      </dd>
    </dl>
  ) : (
    <dl>
      <dt>{trackNum}</dt>
      <dd>
        <div>{current}</div>
        <div>{name}</div>
        <div className={styles.settingsTrackDisplayEffectAreaCls}>
          <div>Distortion: {percent(tracks[trackNum - 1]?.d)}</div>
          <div>Reverb: {percent(tracks[trackNum - 1]?.r)}</div>
        </div>
      </dd>
    </dl>
  );

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
   * リセット確認モーダルを描画する
   */
  const resetModal = process.browser ? (
    <Modal>
      <div className={styles.modalStyleCls}>
        <h1>RESET PATTERN</h1>
        <p>Are you sure you want to reset pattern?</p>
        <footer>
          <SquareButton label="CANCEL" small onClick={closeModal} />
          <SquareButton label="OK" small onClick={handleResetOKBtnClick} />
        </footer>
      </div>
    </Modal>
  ) : (
    <></>
  );

  /**
   * 各パッドを描画する
   */
  const pads = matrix.map((track, row) => {
    const pad = track.map((_, col) => {
      const isPushed = !!matrix[row][col];
      const isCurrent = col === step && master.play;
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
      <div key={row} className={styles.padsCls}>
        {pad}
      </div>
    );
  });

  const knobs = samples[selectedTrack] ? (
    <>
      <Knob
        label="Distortion"
        value={tracks.effects[selectedTrack]?.d}
        onUpdate={handleTrackDistortionKnobCtl}
        onCommit={handleTrackEffectKnobCtl}
      />
      <Knob
        label="Reverb"
        value={tracks.effects[selectedTrack]?.r}
        onUpdate={handleTrackReverbKnobCtl}
        onCommit={handleTrackEffectKnobCtl}
      />
    </>
  ) : (
    <>
      <Knob
        label="Volume"
        value={master.volume}
        onUpdate={handleMasterVolumeKnobCtl}
      />
    </>
  );

  return (
    <>
      <div className={styles.containerCls} id="root">
        <Head>
          <title>Seq</title>
          <meta name="description" content="seq" />
          <link rel="icon" href="/favicon.ico" />
        </Head>
        <MainLayout className={styles.mainFrameCls}>
          <h1 className={styles.titleCls}>Seq</h1>
          <div className={styles.settingsCls}>
            <div className={styles.settingsTrackCls}>
              <div className={styles.settingsTrackDisplayCls}>{display}</div>
            </div>
            <div className={styles.settingsBpmCls}>
              <CircleButton
                label="＋"
                onClick={() => {
                  handleBpmBtnClick(true);
                }}
              />
              <CircleButton label="−" onClick={handleBpmBtnClick} />
            </div>
            <div>
              <Digits label={"BPM"} bpm={master.bpm} />
            </div>
          </div>
          <div className={styles.settingsAreaCls}>
            <div className={styles.settingsTrackButtonAreaCls}>
              <div>Track</div>
              <div>{trackBtns}</div>
            </div>
            <div className={styles.settingsTrackKnobAreaCls}>{knobs}</div>
          </div>
          <div className={styles.controlsCls}>
            <PlayButton pushed={master.play} onClick={handlePlayBtnClick} />
            <div>
              <SquareButton label="RESET" onClick={handleResetBtnClick} />
            </div>
          </div>
          <div className={styles.padsWrapperCls}>{pads}</div>
        </MainLayout>
      </div>
      {resetModal}
    </>
  );
};

export default Seq;
