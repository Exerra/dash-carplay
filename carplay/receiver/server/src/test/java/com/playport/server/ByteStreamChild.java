package com.playport.server;

/** Real process/pipe fixture; no Bluetooth hardware or Python dependency. */
public final class ByteStreamChild {
    public static void main(String[] args) throws Exception {
        if (args[0].equals("pattern")) {
            byte[] pattern = new byte[65536];
            for (int i = 0; i < pattern.length; i++) pattern[i] = (byte)i;
            System.out.write(pattern);
            System.out.flush();
        } else if (args[0].equals("echo")) {
            byte[] buffer = new byte[8192];
            int length;
            while ((length = System.in.read(buffer)) != -1) {
                System.out.write(buffer, 0, length);
                System.out.flush();
            }
        } else {
            Thread.sleep(60000);
        }
    }
}
