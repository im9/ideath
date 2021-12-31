import type { NextPage } from "next";
import dynamic from "next/dynamic";
import Head from "next/head";
import { useState, useEffect, useCallback } from "react";
import * as Tone from "tone";

const AuidioVisualSSR = dynamic(
  () => import("@/components/organismus/AuidioVisual"),
  {
    ssr: false,
  }
);

import * as styles from "@/styles/rec.css";

/**
 * サンプラー
 */
const Rec: NextPage = () => {
  const [recorder, setRecorder] = useState<any>(null);
  const [mic, setMic] = useState<any>(null);
  const [player, setPlayer] = useState<any>({});
  const [blobUrl, setBlobUrl] = useState<string>("");
  const [isRecording, setIsRecording] = useState<boolean>(false);

  const startRecording = useCallback(() => {
    Tone.context.resume();

    setMic(new Tone.UserMedia());
    setRecorder(new Tone.Recorder());
  }, []);

  const handlePlayBtn = useCallback(() => {
    player?.start();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [recorder, player]);

  const stopRecording = useCallback(async () => {
    const data = await recorder?.stop();
    const blobUrl = URL.createObjectURL(data);
    const player = new Tone.Player(blobUrl).toDestination();
    setPlayer(player);
    setBlobUrl(blobUrl);

    setIsRecording(false);
  }, [recorder]);

  useEffect(() => {
    if (recorder) {
      mic?.connect(recorder);
      mic?.open();

      recorder?.start();
      setIsRecording(true);
    }
  }, [mic, recorder]);

  return (
    <div className={styles.containerCls}>
      <Head>
        <title>iDeath</title>
        <meta name="description" content="Generated by create next app" />
        <meta name="viewport" content="initial-scale=1.0, width=device-width" />
        <link rel="icon" href="/favicon.ico" />
      </Head>

      <main className={styles.mainCls}>
        <h1 className={styles.titleCls}>Rec</h1>
        <div>
          <button onClick={startRecording} disabled={isRecording}>
            record
          </button>
          <button onClick={stopRecording} disabled={!isRecording}>
            stop
          </button>
          <button onClick={handlePlayBtn} disabled={!blobUrl}>
            Play
          </button>
        </div>
        <div>
          <AuidioVisualSSR url={blobUrl} />
        </div>
      </main>
    </div>
  );
};

export default Rec;
