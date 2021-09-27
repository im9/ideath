import React, { useState, useEffect, useCallback, useMemo } from "react";
import * as Tone from "tone";
import Pad from "@/components/atoms/Pad";
// import Kick from "@/utils/kick";
import Snare from "@/utils/snare";
import Hihat from "@/utils/hihat";
import Clap from "@/utils/clap";
import Audio from "@/utils/audio";
import styles from "./Pads.module.scss";

type Props = {};

const Music: React.FC<Props> = () => {
  const [audio, setAudio]: any = useState({});

  useEffect(() => {
    setAudio(new Audio());
  }, [setAudio]);

  // パッドにアサインする楽器を初期化
  const pad1 = useMemo(() => {
    if (!process.browser) return;
    const instrument = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return instrument.connect(distortion);
  }, []);

  const pad2 = useMemo(() => {
    if (!process.browser) return;
    const instrument = new Tone.Player("/samples/voice/ah.wav").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return instrument.connect(distortion);
  }, []);

  const pad3 = useMemo(() => {
    if (!process.browser) return;
    const instrument = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return instrument.connect(distortion);
  }, []);

  const pad4 = useMemo(() => {
    if (!process.browser) return;
    const instrument = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return instrument.connect(distortion);
  }, []);

  const pad5 = useMemo(() => {
    if (!process.browser) return;
    return new Hihat(audio.ctx);
  }, [audio.ctx]);

  const pad6 = useMemo(() => {
    if (!process.browser) return;
    const instrument = new Tone.Player("/samples/808/CP.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    return instrument.connect(distortion);
  }, []);

  const pad7 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player(
      "/samples/sd/rytm-00-hard.wav"
    ).toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad8 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player("/samples/techno/001_1.wav").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad9 = useMemo(() => {
    return new Snare(audio.ctx);
  }, [audio.ctx]);

  const pad10 = useMemo(() => {
    return new Clap(audio.ctx);
  }, [audio.ctx]);

  const pad11 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player("/samples/808/CP.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad12 = useMemo(() => {
    if (!process.browser) return;
    return new Snare(audio.ctx);
  }, [audio.ctx]);

  const pad13 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player("/samples/808/RS.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad14 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player("/samples/909/BT0A0A7.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad15 = useMemo(() => {
    if (!process.browser) return;
    const player = new Tone.Player("/samples/808bd/BD0025.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  const pad16 = useMemo(() => {
    if (!process.browser) return;

    const player = new Tone.Player("/samples/808bd/BD0010.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    return player.connect(filter);
  }, []);

  // 1
  const handleS1Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad1?.start();
    });
  }, [pad1]);

  // 2
  const handleS2Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad2?.start();
    });
  }, [pad2]);

  // 3
  const handleS3Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad3?.start();
    });
  }, [pad3]);

  // 4
  const handleS4Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad4?.start();
    });
  }, [pad4]);

  // 5
  const handleHHClick = useCallback(() => {
    pad5?.trigger(audio.ctx.currentTime);
  }, [pad5, audio.ctx]);

  // 6
  const handleHH2Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad6?.start();
    });
  }, [pad6]);

  // 7
  const handleHH3Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad7?.start();
    });
  }, [pad7]);

  // 8
  const handleHH4Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad8?.start();
    });
  }, [pad8]);

  // 9
  const handleSnareClick = useCallback(() => {
    pad9?.trigger(audio.ctx.currentTime);
  }, [pad9, audio.ctx]);

  // 10
  const handleSnare2Click = useCallback(() => {
    pad10?.trigger(audio.ctx.currentTime);
  }, [pad10, audio.ctx]);

  // 11
  const handleSnare3Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad11?.start();
    });
  }, [pad11]);

  // 12
  const handleSnare4Click = useCallback(() => {
    pad12?.trigger(audio.ctx.currentTime);
  }, [pad12, audio.ctx]);

  // 13
  const handleKickClick = useCallback(() => {
    Tone.loaded().then(() => {
      pad13?.start();
    });
  }, [pad13]);

  // 14
  const handleKick2Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad14?.start();
    });
  }, [pad14]);

  // 15
  const handleKick3Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad15?.start();
    });
  }, [pad15]);

  // 16
  const handleKick4Click = useCallback(() => {
    Tone.loaded().then(() => {
      pad16?.start();
    });
  }, [pad16]);

  return (
    <div className={styles.Pads}>
      <Pad clickPad={handleS1Click} />
      <Pad clickPad={handleS2Click} />
      <Pad clickPad={handleS3Click} />
      <Pad clickPad={handleS4Click} />

      <Pad clickPad={handleHHClick} />
      <Pad clickPad={handleHH2Click} />
      <Pad clickPad={handleHH3Click} />
      <Pad clickPad={handleHH4Click} />

      <Pad clickPad={handleSnareClick} />
      <Pad clickPad={handleSnare2Click} />
      <Pad clickPad={handleSnare3Click} />
      <Pad clickPad={handleSnare4Click} />

      <Pad clickPad={handleKickClick} />
      <Pad clickPad={handleKick2Click} />
      <Pad clickPad={handleKick3Click} />
      <Pad clickPad={handleKick4Click} />
    </div>
  );
};

export default Music;
