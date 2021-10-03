import type { NextPage } from "next";
import Head from "next/head";
// import Image from "next/image";
import Pads from "@/components/Pads";
import styles from "@/styles/Home.module.scss";

/**
 * パッド
 */
const Home: NextPage = () => {
  return (
    <div className={styles.container}>
      <Head>
        <title>Create Next App</title>
        <meta name="description" content="Generated by create next app" />
        <meta name="viewport" content="initial-scale=1.0, width=device-width" />
        <link rel="icon" href="/favicon.ico" />
      </Head>

      <main className={styles.main}>
        <div>
          <Pads />
        </div>
      </main>
    </div>
  );
};

export default Home;