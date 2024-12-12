from flask import Flask, render_template

app = Flask(__name__)

# 定义主页路由
@app.route('/')
def index():
    return render_template('index.html')  # 渲染HTML页面

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=80)  # 使用HTTP协议的默认端口80
