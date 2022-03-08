import cv2
import paho.mqtt.client as mqtt
from time import sleep
filepath = "vtest.avi"
# Webカメラを使うときはこちら
cap = cv2.VideoCapture(0)

avg = None
client = mqtt.Client()

client.connect("ServerIP", 1883, 60)

client.loop_start()
cnt = 0
while True:
    # 1フレームずつ取得する。
    ret, frame = cap.read()
    if not ret:
        break

    # グレースケールに変換
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    # 比較用のフレームを取得する
    if avg is None:
        avg = gray.copy().astype("float")
        continue

    # 現在のフレームと移動平均との差を計算
    cv2.accumulateWeighted(gray, avg, 0.6)
    frameDelta = cv2.absdiff(gray, cv2.convertScaleAbs(avg))

    # デルタ画像を閾値処理を行う
    #thresh = cv2.threshold(frameDelta, 3, 255, cv2.THRESH_BINARY)[1]
    # 画像の閾値に輪郭線を入れる
    #contours, hierarchy = cv2.findContours(thresh.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    #frame = cv2.drawContours(frame, contours, -1, (0, 255, 0), 3)
    color = 0
    if(frameDelta >= 48).any():
        print("1")
        color = 1
    else:
        print("0")
        color = 0
    
    if(cnt == 0):
        client.publish("TopicName", "{color:" + str(color) + "}")
    # 結果を出力
    cv2.imshow("Frame", frame)
    cv2.imshow("frameDelta", frameDelta)
    key = cv2.waitKey(30)
    if key == 27:
        break
    cnt += 1
    if(cnt == 30):
        cnt = 0
cap.release()
cv2.destroyAllWindows()
