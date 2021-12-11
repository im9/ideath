import * as Tone from "tone";
import type { NextPage } from "next";
import Head from "next/head";
import { useMemo, useState, useEffect } from "react";
import Qwerty from "@/components/organismus/Qwerty";
import { DEFAULT_QWERTY_VALUES } from "@/constants/org";
import * as styles from "@/styles/org.css";

/**
 * トラッカー
 */
const Org: NextPage = () => {
  // state
  const [qwerty, setQwerty] = useState(DEFAULT_QWERTY_VALUES);

  // If pressed key is our target key then set to true
  const downHandler = ({ key }: any) => {
    if (key) {
      setQwerty(() => {
        return {
          ...qwerty,
          [key]: true,
        };
      });
    }
  };
  // If released key is our target key then set to false
  const upHandler = ({ key }: any) => {
    if (key) {
      setQwerty(() => {
        return {
          ...qwerty,
          [key]: false,
        };
      });
    }
  };

  // Add event listeners
  useEffect(() => {
    window.addEventListener("keydown", downHandler);
    window.addEventListener("keyup", upHandler);

    // Remove event listeners on cleanup
    return () => {
      window.removeEventListener("keydown", downHandler);
      window.removeEventListener("keyup", upHandler);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <div className={styles.containerCls}>
      <Head>
        <title>iDeath</title>
        <meta name="description" content="Generated by create next app" />
        <meta name="viewport" content="initial-scale=1.0, width=device-width" />
        <link rel="icon" href="/favicon.ico" />
      </Head>

      <main className={styles.mainCls}>
        <h1 className={styles.titleCls}>Org</h1>
        <div>
          <Qwerty pushedList={qwerty} />
        </div>
      </main>
    </div>
  );
};

export default Org;
