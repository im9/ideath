import React, { useCallback } from "react";
import { useRouter } from "next/router";
import { mainCls, btnAreaCls, logoCls } from "./MainLayout.css";

type Props = {
  children: any;
  className?: String;
};

/**
 * 共通レイアウトを描画する
 */
const Layout: React.FC<Props> = ({ children, className = "" }) => {
  const router = useRouter();

  /**
   * ロゴのクリックをハンドルする
   */
  const handleLogoClick = useCallback(() => {
    router.push("/");
  }, [router]);

  return (
    <main className={`${mainCls} ${className}`}>
      <div className={btnAreaCls}>
        <button className={logoCls} onClick={handleLogoClick}>
          iDeath
        </button>
      </div>
      {children}
    </main>
  );
};

export default Layout;
