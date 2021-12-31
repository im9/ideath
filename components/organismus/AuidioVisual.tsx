import React from "react";
import { useRef, useEffect } from "react";
import WaveSurfer from "wavesurfer.js";

type Props = {
  url?: String;
};

const AuidioVisual: React.FC<Props> = ({ url }) => {
  const waveformRef = useRef<any>(null);

  useEffect(() => {}, []);

  useEffect(() => {
    if (url) {
      waveformRef.current = WaveSurfer.create({
        container: waveformRef.current,
      });
      console.log(url);
      waveformRef.current.load(url);
    }
  }, [url]);

  return <div ref={waveformRef}></div>;
};

export default AuidioVisual;
