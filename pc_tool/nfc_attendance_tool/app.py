from __future__ import annotations

import queue
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

from .database import Database, Person
from .image_codec import (
    load_portrait,
    portrait_blocks,
    preview_image,
    render_text_bitmap,
    text_blocks,
)
from .protocol import (
    CardType,
    DeviceConfigPayload,
    PersonPayload,
    build_clear,
    build_config_commands,
    build_config_query,
    build_issue,
    build_list,
    build_read,
    build_time_query,
    build_update_image,
    build_weather_force_query,
    build_weather_query,
    build_weather_test,
    chunk_commands,
    normalize_uid,
)
from .serial_client import SerialClient, available_ports


APP_TITLE = "NFC 考勤系统上位机"
DEFAULT_BAUDRATES = ("115200", "9600", "57600", "38400")


class AttendanceApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1180x760")
        self.minsize(1040, 680)

        base_dir = Path(__file__).resolve().parents[1]
        self.data_dir = base_dir / "data"
        self.db = Database(self.data_dir / "attendance.db")
        self.ui_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.client = SerialClient(on_line=self.on_serial_line)
        self.serial_busy = False
        self.serial_buttons: list[ttk.Button] = []

        self.portrait_path: Path | None = None
        self.portrait_image: Image.Image | None = None
        self.name_image: Image.Image | None = None
        self.department_image: Image.Image | None = None
        self.photo_refs: dict[str, ImageTk.PhotoImage] = {}

        self.uid_var = tk.StringVar()
        self.sid_var = tk.StringVar()
        self.name_var = tk.StringVar()
        self.department_var = tk.StringVar()
        self.points_var = tk.StringVar(value="0")
        self.card_type_var = tk.StringVar(value="图像卡")
        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="115200")
        self.list_count_var = tk.StringVar(value="20")
        self.threshold_var = tk.IntVar(value=150)
        self.status_var = tk.StringVar(value="未连接")
        self.cfg_device_id_var = tk.StringVar(value="1")
        self.cfg_work_mode_var = tk.StringVar(value="进出")
        self.cfg_upload_enable_var = tk.BooleanVar(value=True)
        self.cfg_repeat_var = tk.StringVar(value="60")
        self.cfg_wifi_ssid_var = tk.StringVar()
        self.cfg_wifi_password_var = tk.StringVar()
        self.cfg_server_host_var = tk.StringVar(value="192.168.1.10")
        self.cfg_server_port_var = tk.StringVar(value="9000")
        self.cfg_weather_key_var = tk.StringVar()
        self.cfg_weather_location_var = tk.StringVar(value="hangzhou")
        self.cfg_weather_test_var = tk.StringVar(value="Sunny 20C")
        self.cfg_timezone_var = tk.StringVar(value="8")

        self.build_styles()
        self.build_layout()
        self.refresh_ports()
        self.refresh_people()
        self.refresh_records()
        self.update_previews()
        self.after(80, self.process_ui_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def build_styles(self) -> None:
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("TFrame", padding=0)
        style.configure("Panel.TLabelframe", padding=10)
        style.configure("Panel.TLabelframe.Label", font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Action.TButton", padding=(12, 6))
        style.configure("Status.TLabel", padding=(8, 5))

    def build_layout(self) -> None:
        root = ttk.Frame(self, padding=12)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(0, weight=0, minsize=360)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(1, weight=1)

        self.build_connection_panel(root)
        self.build_person_panel(root)
        self.build_tabs(root)
        self.build_status_bar(root)

    def build_connection_panel(self, parent: ttk.Frame) -> None:
        panel = ttk.LabelFrame(parent, text="串口连接", style="Panel.TLabelframe")
        panel.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 10))
        panel.columnconfigure(1, weight=1)

        ttk.Label(panel, text="端口").grid(row=0, column=0, padx=(0, 6), pady=4, sticky="w")
        self.port_combo = ttk.Combobox(panel, textvariable=self.port_var, width=24, state="readonly")
        self.port_combo.grid(row=0, column=1, pady=4, sticky="ew")
        ttk.Button(panel, text="刷新", command=self.refresh_ports).grid(row=0, column=2, padx=6, pady=4)

        ttk.Label(panel, text="波特率").grid(row=0, column=3, padx=(12, 6), pady=4)
        ttk.Combobox(panel, textvariable=self.baud_var, values=DEFAULT_BAUDRATES, width=10).grid(
            row=0, column=4, pady=4
        )
        self.connect_btn = ttk.Button(panel, text="打开串口", style="Action.TButton", command=self.toggle_port)
        self.connect_btn.grid(row=0, column=5, padx=(12, 0), pady=4)

        self.read_btn = ttk.Button(panel, text="读卡", command=self.read_card)
        self.read_btn.grid(row=0, column=6, padx=(12, 0), pady=4)
        self.clear_btn = ttk.Button(panel, text="清卡", command=self.clear_card)
        self.clear_btn.grid(row=0, column=7, padx=(6, 0), pady=4)
        self.serial_buttons.extend([self.read_btn, self.clear_btn])

    def build_person_panel(self, parent: ttk.Frame) -> None:
        panel = ttk.LabelFrame(parent, text="发卡信息", style="Panel.TLabelframe")
        panel.grid(row=1, column=0, sticky="nsew", padx=(0, 10))
        panel.columnconfigure(1, weight=1)

        fields = [
            ("UID", self.uid_var),
            ("工号", self.sid_var),
            ("姓名", self.name_var),
            ("部门", self.department_var),
            ("积分", self.points_var),
        ]
        for row, (label, var) in enumerate(fields):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky="w", pady=5)
            entry = ttk.Entry(panel, textvariable=var)
            entry.grid(row=row, column=1, sticky="ew", pady=5)

        ttk.Label(panel, text="卡类型").grid(row=5, column=0, sticky="w", pady=5)
        ttk.Combobox(
            panel,
            textvariable=self.card_type_var,
            values=("普通卡", "图像卡", "管理员卡"),
            state="readonly",
        ).grid(row=5, column=1, sticky="ew", pady=5)

        ttk.Label(panel, text="二值阈值").grid(row=6, column=0, sticky="w", pady=5)
        threshold = ttk.Scale(
            panel,
            from_=40,
            to=230,
            orient=tk.HORIZONTAL,
            variable=self.threshold_var,
            command=lambda _value: self.update_previews(),
        )
        threshold.grid(row=6, column=1, sticky="ew", pady=5)

        image_buttons = ttk.Frame(panel)
        image_buttons.grid(row=7, column=0, columnspan=2, sticky="ew", pady=(8, 6))
        image_buttons.columnconfigure((0, 1), weight=1)
        ttk.Button(image_buttons, text="选择头像", command=self.choose_portrait).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ttk.Button(image_buttons, text="重新预览", command=self.update_previews).grid(row=0, column=1, sticky="ew", padx=(4, 0))

        preview_box = ttk.Frame(panel)
        preview_box.grid(row=8, column=0, columnspan=2, sticky="ew", pady=(6, 8))
        preview_box.columnconfigure((0, 1), weight=1)
        self.portrait_preview = ttk.Label(preview_box, anchor="center", relief=tk.SOLID)
        self.portrait_preview.grid(row=0, column=0, rowspan=2, sticky="nsew", padx=(0, 8))
        self.name_preview = ttk.Label(preview_box, anchor="center", relief=tk.SOLID)
        self.name_preview.grid(row=0, column=1, sticky="ew", pady=(0, 8))
        self.department_preview = ttk.Label(preview_box, anchor="center", relief=tk.SOLID)
        self.department_preview.grid(row=1, column=1, sticky="ew")

        action_box = ttk.Frame(panel)
        action_box.grid(row=9, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        action_box.columnconfigure((0, 1), weight=1)
        ttk.Button(action_box, text="保存人员", command=self.save_person).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.issue_btn = ttk.Button(action_box, text="发卡写卡", style="Action.TButton", command=self.issue_card)
        self.issue_btn.grid(row=0, column=1, sticky="ew", padx=(4, 0))
        self.serial_buttons.append(self.issue_btn)

        self.image_only_btn = ttk.Button(panel, text="仅发送图像块", command=self.send_images_only)
        self.image_only_btn.grid(row=10, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.serial_buttons.append(self.image_only_btn)

    def build_tabs(self, parent: ttk.Frame) -> None:
        tabs = ttk.Notebook(parent)
        tabs.grid(row=1, column=1, sticky="nsew")
        parent.rowconfigure(1, weight=1)

        self.people_tab = ttk.Frame(tabs, padding=8)
        self.records_tab = ttk.Frame(tabs, padding=8)
        self.config_tab = ttk.Frame(tabs, padding=8)
        self.log_tab = ttk.Frame(tabs, padding=8)
        tabs.add(self.people_tab, text="人员管理")
        tabs.add(self.records_tab, text="考勤记录")
        tabs.add(self.config_tab, text="设备配置")
        tabs.add(self.log_tab, text="通信日志")

        self.build_people_tab()
        self.build_records_tab()
        self.build_config_tab()
        self.build_log_tab()

    def build_people_tab(self) -> None:
        self.people_tab.rowconfigure(0, weight=1)
        self.people_tab.columnconfigure(0, weight=1)

        columns = ("uid", "sid", "name", "department", "type", "points", "lost", "updated")
        self.people_tree = ttk.Treeview(self.people_tab, columns=columns, show="headings", height=12)
        headings = {
            "uid": "UID",
            "sid": "工号",
            "name": "姓名",
            "department": "部门",
            "type": "类型",
            "points": "积分",
            "lost": "挂失",
            "updated": "更新时间",
        }
        widths = {"uid": 100, "sid": 80, "name": 90, "department": 110, "type": 70, "points": 60, "lost": 60, "updated": 150}
        for key in columns:
            self.people_tree.heading(key, text=headings[key])
            self.people_tree.column(key, width=widths[key], anchor=tk.CENTER)
        self.people_tree.grid(row=0, column=0, sticky="nsew")
        self.people_tree.bind("<<TreeviewSelect>>", self.load_selected_person)
        people_scroll = ttk.Scrollbar(self.people_tab, orient=tk.VERTICAL, command=self.people_tree.yview)
        people_scroll.grid(row=0, column=1, sticky="ns")
        self.people_tree.configure(yscrollcommand=people_scroll.set)

        actions = ttk.Frame(self.people_tab)
        actions.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(actions, text="刷新", command=self.refresh_people).pack(side=tk.LEFT)
        ttk.Button(actions, text="标记挂失", command=lambda: self.set_selected_lost(True)).pack(side=tk.LEFT, padx=6)
        ttk.Button(actions, text="取消挂失", command=lambda: self.set_selected_lost(False)).pack(side=tk.LEFT)

    def build_records_tab(self) -> None:
        self.records_tab.rowconfigure(1, weight=1)
        self.records_tab.columnconfigure(0, weight=1)

        controls = ttk.Frame(self.records_tab)
        controls.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        ttk.Label(controls, text="查询条数").pack(side=tk.LEFT)
        ttk.Entry(controls, textvariable=self.list_count_var, width=8).pack(side=tk.LEFT, padx=6)
        self.query_records_btn = ttk.Button(controls, text="从下位机读取", command=self.query_records)
        self.query_records_btn.pack(side=tk.LEFT)
        self.serial_buttons.append(self.query_records_btn)
        ttk.Button(controls, text="刷新本地", command=self.refresh_records).pack(side=tk.LEFT, padx=6)
        ttk.Button(controls, text="导出 CSV", command=self.export_records).pack(side=tk.LEFT)

        columns = ("seq", "uid", "sid", "rtype", "time", "device", "status", "imported")
        self.records_tree = ttk.Treeview(self.records_tab, columns=columns, show="headings")
        headings = {
            "seq": "序号",
            "uid": "UID",
            "sid": "工号",
            "rtype": "类型",
            "time": "时间",
            "device": "设备",
            "status": "状态",
            "imported": "导入时间",
        }
        widths = {"seq": 60, "uid": 100, "sid": 80, "rtype": 80, "time": 150, "device": 70, "status": 80, "imported": 150}
        for key in columns:
            self.records_tree.heading(key, text=headings[key])
            self.records_tree.column(key, width=widths[key], anchor=tk.CENTER)
        self.records_tree.grid(row=1, column=0, sticky="nsew")
        records_scroll = ttk.Scrollbar(self.records_tab, orient=tk.VERTICAL, command=self.records_tree.yview)
        records_scroll.grid(row=1, column=1, sticky="ns")
        self.records_tree.configure(yscrollcommand=records_scroll.set)

    def build_config_tab(self) -> None:
        self.config_tab.columnconfigure(1, weight=1)
        self.config_tab.columnconfigure(3, weight=1)

        fields = [
            ("设备 ID", self.cfg_device_id_var, 0, 0),
            ("工作模式", self.cfg_work_mode_var, 0, 2),
            ("防重复秒", self.cfg_repeat_var, 1, 0),
            ("时区", self.cfg_timezone_var, 1, 2),
            ("WiFi SSID", self.cfg_wifi_ssid_var, 2, 0),
            ("WiFi 密码", self.cfg_wifi_password_var, 2, 2),
            ("服务器", self.cfg_server_host_var, 3, 0),
            ("端口", self.cfg_server_port_var, 3, 2),
            ("天气 Key", self.cfg_weather_key_var, 4, 0),
            ("天气位置", self.cfg_weather_location_var, 4, 2),
        ]
        for label, var, row, col in fields:
            ttk.Label(self.config_tab, text=label).grid(row=row, column=col, sticky="w", padx=(0, 6), pady=5)
            if var is self.cfg_work_mode_var:
                widget = ttk.Combobox(
                    self.config_tab,
                    textvariable=var,
                    values=("普通", "签到", "签退", "进出"),
                    state="readonly",
                )
            else:
                show = "*" if var is self.cfg_wifi_password_var else ""
                widget = ttk.Entry(self.config_tab, textvariable=var, show=show)
            widget.grid(row=row, column=col + 1, sticky="ew", padx=(0, 14), pady=5)

        ttk.Checkbutton(self.config_tab, text="启用上传", variable=self.cfg_upload_enable_var).grid(
            row=5, column=0, columnspan=2, sticky="w", pady=(6, 0)
        )

        ttk.Label(self.config_tab, text="测试天气").grid(row=5, column=2, sticky="w", padx=(0, 6), pady=5)
        ttk.Entry(self.config_tab, textvariable=self.cfg_weather_test_var).grid(
            row=5, column=3, sticky="ew", padx=(0, 14), pady=5
        )

        actions = ttk.Frame(self.config_tab)
        actions.grid(row=6, column=0, columnspan=4, sticky="ew", pady=(12, 0))
        self.query_config_btn = ttk.Button(actions, text="读取配置", command=self.query_config)
        self.query_config_btn.pack(side=tk.LEFT)
        self.write_config_btn = ttk.Button(actions, text="写入配置", style="Action.TButton", command=self.write_config)
        self.write_config_btn.pack(side=tk.LEFT, padx=8)
        self.query_weather_btn = ttk.Button(actions, text="读取天气", command=self.query_weather)
        self.query_weather_btn.pack(side=tk.LEFT)
        self.write_weather_test_btn = ttk.Button(actions, text="写测试天气", command=self.write_weather_test)
        self.write_weather_test_btn.pack(side=tk.LEFT, padx=8)
        self.force_weather_btn = ttk.Button(actions, text="强制查天气", command=self.force_weather_query)
        self.force_weather_btn.pack(side=tk.LEFT)
        self.query_time_btn = ttk.Button(actions, text="读取时间", command=self.query_time)
        self.query_time_btn.pack(side=tk.LEFT, padx=8)
        self.serial_buttons.extend([
            self.query_config_btn,
            self.write_config_btn,
            self.query_weather_btn,
            self.write_weather_test_btn,
            self.force_weather_btn,
            self.query_time_btn,
        ])

    def build_log_tab(self) -> None:
        self.log_tab.rowconfigure(0, weight=1)
        self.log_tab.columnconfigure(0, weight=1)
        self.log_text = tk.Text(self.log_tab, wrap=tk.WORD, height=12, font=("Consolas", 10))
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(self.log_tab, orient=tk.VERTICAL, command=self.log_text.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scroll.set)
        ttk.Button(self.log_tab, text="清空日志", command=lambda: self.log_text.delete("1.0", tk.END)).grid(
            row=1, column=0, sticky="w", pady=(8, 0)
        )

    def build_status_bar(self, parent: ttk.Frame) -> None:
        status = ttk.Label(parent, textvariable=self.status_var, style="Status.TLabel", relief=tk.SUNKEN, anchor=tk.W)
        status.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(10, 0))

    def refresh_ports(self) -> None:
        ports = available_ports()
        values = [f"{port.device} - {port.description}" for port in ports]
        self.port_combo.configure(values=values)
        if values and not self.port_var.get():
            self.port_var.set(values[0])

    def selected_port(self) -> str:
        value = self.port_var.get().strip()
        if " - " in value:
            return value.split(" - ", 1)[0]
        return value

    def toggle_port(self) -> None:
        if self.client.is_open:
            self.client.close()
            self.connect_btn.configure(text="打开串口")
            self.status_var.set("未连接")
            self.append_log("串口已关闭")
            return
        port = self.selected_port()
        if not port:
            messagebox.showwarning(APP_TITLE, "请选择串口")
            return
        try:
            baudrate = int(self.baud_var.get())
            self.client.open(port, baudrate)
        except Exception as exc:
            messagebox.showerror(APP_TITLE, f"打开串口失败：{exc}")
            return
        self.connect_btn.configure(text="关闭串口")
        self.status_var.set(f"已连接 {port} @ {self.baud_var.get()}")
        self.append_log(f"串口已打开：{port}")

    def choose_portrait(self) -> None:
        path = filedialog.askopenfilename(
            title="选择头像",
            filetypes=[("图片文件", "*.png;*.jpg;*.jpeg;*.bmp"), ("所有文件", "*.*")],
        )
        if not path:
            return
        self.portrait_path = Path(path)
        self.update_previews()

    def update_previews(self) -> None:
        try:
            threshold = self.threshold_var.get()
            if self.portrait_path:
                self.portrait_image = load_portrait(self.portrait_path, threshold)
            else:
                self.portrait_image = Image.new("1", (48, 64), 255)
            self.name_image = render_text_bitmap(self.name_var.get())
            self.department_image = render_text_bitmap(self.department_var.get())
            self.set_preview("portrait", self.portrait_preview, preview_image(self.portrait_image, 3))
            self.set_preview("name", self.name_preview, preview_image(self.name_image, 3))
            self.set_preview("department", self.department_preview, preview_image(self.department_image, 3))
        except Exception as exc:
            self.append_log(f"预览生成失败：{exc}")

    def set_preview(self, key: str, label: ttk.Label, image: Image.Image) -> None:
        photo = ImageTk.PhotoImage(image)
        self.photo_refs[key] = photo
        label.configure(image=photo)

    def card_type_value(self) -> CardType:
        mapping = {"普通卡": CardType.NORMAL, "图像卡": CardType.IMAGE, "管理员卡": CardType.ADMIN}
        return mapping[self.card_type_var.get()]

    def collect_person(self) -> Person:
        uid = normalize_uid(self.uid_var.get())
        try:
            sid = int(self.sid_var.get())
            points = int(self.points_var.get())
        except ValueError as exc:
            raise ValueError("工号和积分必须是整数") from exc
        if sid < 0 or sid > 0xFFFFFFFF:
            raise ValueError("工号必须在 0..4294967295 范围内")
        if points < 0 or points > 0xFFFFFFFF:
            raise ValueError("积分必须在 0..4294967295 范围内")
        name = self.name_var.get().strip()
        department = self.department_var.get().strip()
        if not name:
            raise ValueError("姓名不能为空")
        if not department:
            raise ValueError("部门不能为空")
        return Person(uid, sid, name, department, int(self.card_type_value()), points)

    def save_person(self) -> None:
        try:
            person = self.collect_person()
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        self.db.upsert_person(person)
        self.refresh_people()
        self.status_var.set("人员信息已保存")

    def read_card(self) -> None:
        self.run_serial_job("读卡", [build_read()], expect_multi=True)

    def clear_card(self) -> None:
        try:
            uid = normalize_uid(self.uid_var.get())
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        if not messagebox.askyesno(APP_TITLE, f"确认清空 UID {uid} 的卡片数据？"):
            return
        self.run_serial_job("清卡", [build_clear(uid)])

    def issue_card(self) -> None:
        try:
            person = self.collect_person()
            self.db.upsert_person(person)
            commands = self.build_issue_commands(person)
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        self.run_serial_job("发卡", commands, person=person, action="ISSUE")

    def send_images_only(self) -> None:
        try:
            person = self.collect_person()
            commands = self.build_image_commands(person.name, person.department)
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        self.run_serial_job("发送图像", commands)

    def build_issue_commands(self, person: Person) -> list[str]:
        payload = PersonPayload(
            uid_hex=person.uid_hex,
            sid=person.sid,
            points=person.points,
            card_type=CardType(person.card_type),
        )
        commands = [build_issue(payload)]
        if CardType(person.card_type) == CardType.IMAGE:
            commands.extend(self.build_image_commands(person.name, person.department))
        return commands

    def build_image_commands(self, name: str, department: str) -> list[str]:
        threshold = self.threshold_var.get()
        portrait = load_portrait(self.portrait_path, threshold) if self.portrait_path else Image.new("1", (48, 64), 255)
        name_image = render_text_bitmap(name)
        department_image = render_text_bitmap(department)
        commands: list[str] = []
        commands.extend(chunk_commands("IMGA", portrait_blocks(portrait)))
        commands.extend(chunk_commands("IMGN", text_blocks(name_image)))
        commands.extend(chunk_commands("IMGD", text_blocks(department_image)))
        commands.append(build_update_image())
        return commands

    def query_records(self) -> None:
        try:
            count = int(self.list_count_var.get())
            command = build_list(count)
        except ValueError:
            command = build_list(None)
        self.run_serial_job("查询记录", [command], expect_multi=True, import_records=True)

    def collect_device_config(self) -> DeviceConfigPayload:
        modes = {"普通": 0, "签到": 1, "签退": 2, "进出": 3}
        try:
            device_id = int(self.cfg_device_id_var.get())
            repeat = int(self.cfg_repeat_var.get())
            port = int(self.cfg_server_port_var.get())
            timezone = int(self.cfg_timezone_var.get())
        except ValueError as exc:
            raise ValueError("设备 ID、防重复秒、端口和时区必须是整数") from exc

        return DeviceConfigPayload(
            device_id=device_id,
            work_mode=modes[self.cfg_work_mode_var.get()],
            upload_enable=bool(self.cfg_upload_enable_var.get()),
            repeat_interval_sec=repeat,
            wifi_ssid=self.cfg_wifi_ssid_var.get(),
            wifi_password=self.cfg_wifi_password_var.get(),
            server_host=self.cfg_server_host_var.get(),
            server_port=port,
            weather_key=self.cfg_weather_key_var.get(),
            weather_location=self.cfg_weather_location_var.get(),
            timezone=timezone,
        )

    def query_config(self) -> None:
        self.run_serial_job("读取配置", [build_config_query()], expect_multi=True)

    def write_config(self) -> None:
        try:
            commands = build_config_commands(self.collect_device_config())
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        self.run_serial_job("写入配置", commands)

    def query_weather(self) -> None:
        self.run_serial_job("读取天气", [build_weather_query()], expect_multi=True)

    def write_weather_test(self) -> None:
        try:
            command = build_weather_test(self.cfg_weather_test_var.get())
        except Exception as exc:
            messagebox.showwarning(APP_TITLE, str(exc))
            return
        self.run_serial_job("写测试天气", [command])

    def force_weather_query(self) -> None:
        self.run_serial_job("强制查天气", [build_weather_force_query()], expect_multi=True)

    def query_time(self) -> None:
        self.run_serial_job("读取时间", [build_time_query()], expect_multi=True)

    def run_serial_job(
        self,
        title: str,
        commands: list[str],
        person: Person | None = None,
        action: str = "",
        expect_multi: bool = False,
        import_records: bool = False,
    ) -> None:
        if not self.client.is_open:
            messagebox.showwarning(APP_TITLE, "请先打开串口")
            return
        if self.serial_busy:
            messagebox.showwarning(APP_TITLE, "串口任务正在执行，请等待当前任务完成")
            return
        self.serial_busy = True
        self.set_serial_controls(False)

        def worker() -> None:
            ok = True
            try:
                self.ui_queue.put(("status", f"{title}中..."))
                for index, command in enumerate(commands, 1):
                    self.ui_queue.put(("log", f"> {command.rstrip()}"))
                    timeout = 4.0 if command.startswith("UPDATEIMG") else 2.0
                    lines = self.client.transact(command, timeout=timeout)
                    if not lines:
                        ok = False
                        self.ui_queue.put(("log", "! 等待响应超时"))
                        break
                    for line in lines:
                        if import_records and line.startswith("REC:"):
                            self.db.import_record_line(line)
                    last = lines[-1].strip().upper()
                    if last.startswith("ERR:"):
                        ok = False
                        break
                    if expect_multi:
                        break
                    self.ui_queue.put(("status", f"{title} {index}/{len(commands)}"))
                if person:
                    self.db.add_issue_log(person, action or title, "OK" if ok else "FAILED")
                self.ui_queue.put(("status", f"{title}{'完成' if ok else '失败'}"))
                self.ui_queue.put(("refresh", "all"))
            except Exception as exc:
                ok = False
                self.ui_queue.put(("log", f"! {title}异常：{exc}"))
                self.ui_queue.put(("status", f"{title}失败"))
            finally:
                self.ui_queue.put(("serial_busy", False))

        threading.Thread(target=worker, daemon=True).start()

    def on_serial_line(self, line: str) -> None:
        self.ui_queue.put(("log", f"< {line}"))
        upper = line.strip().upper()
        if upper.startswith("UID:"):
            self.ui_queue.put(("uid", line.split(":", 1)[1].strip()))
        elif upper.startswith("CFG:"):
            self.ui_queue.put(("config", line.strip()))

    def process_ui_queue(self) -> None:
        try:
            while True:
                kind, payload = self.ui_queue.get_nowait()
                if kind == "log":
                    self.append_log(str(payload))
                elif kind == "status":
                    self.status_var.set(str(payload))
                elif kind == "uid":
                    self.uid_var.set(str(payload))
                elif kind == "config":
                    self.apply_config_line(str(payload))
                elif kind == "refresh":
                    self.refresh_people()
                    self.refresh_records()
                elif kind == "serial_busy":
                    self.serial_busy = bool(payload)
                    self.set_serial_controls(not self.serial_busy)
        except queue.Empty:
            pass
        self.after(80, self.process_ui_queue)

    def set_serial_controls(self, enabled: bool) -> None:
        state = tk.NORMAL if enabled else tk.DISABLED
        for button in self.serial_buttons:
            button.configure(state=state)

    def append_log(self, message: str) -> None:
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)

    def apply_config_line(self, line: str) -> None:
        if not line.startswith("CFG:"):
            return
        fields: dict[str, str] = {}
        for part in line[4:].split("|"):
            if "=" in part:
                key, value = part.split("=", 1)
                fields[key.upper()] = value

        mode_names = {"0": "普通", "1": "签到", "2": "签退", "3": "进出"}
        if "DEV" in fields:
            self.cfg_device_id_var.set(fields["DEV"])
        if "MODE" in fields:
            self.cfg_work_mode_var.set(mode_names.get(fields["MODE"], self.cfg_work_mode_var.get()))
        if "UPLOAD" in fields:
            self.cfg_upload_enable_var.set(fields["UPLOAD"] == "1")
        if "REPEAT" in fields:
            self.cfg_repeat_var.set(fields["REPEAT"])
        if "SSID" in fields:
            self.cfg_wifi_ssid_var.set(fields["SSID"])
        if "HOST" in fields:
            self.cfg_server_host_var.set(fields["HOST"])
        if "PORT" in fields:
            self.cfg_server_port_var.set(fields["PORT"])
        if "WLOC" in fields:
            self.cfg_weather_location_var.set(fields["WLOC"])
        if "TZ" in fields:
            self.cfg_timezone_var.set(fields["TZ"])

    def refresh_people(self) -> None:
        for item in self.people_tree.get_children():
            self.people_tree.delete(item)
        type_names = {0: "普通", 1: "图像", 2: "管理员"}
        for row in self.db.list_people():
            self.people_tree.insert(
                "",
                tk.END,
                values=(
                    row["uid_hex"],
                    row["sid"],
                    row["name"],
                    row["department"],
                    type_names.get(row["card_type"], row["card_type"]),
                    row["points"],
                    "是" if row["lost"] else "否",
                    row["updated_at"],
                ),
            )

    def refresh_records(self) -> None:
        for item in self.records_tree.get_children():
            self.records_tree.delete(item)
        for row in self.db.list_attendance():
            self.records_tree.insert(
                "",
                tk.END,
                values=(
                    row["seq"] or "",
                    row["uid_hex"] or "",
                    row["sid"] or "",
                    row["record_type"] or "",
                    row["occurred_at"] or "",
                    row["device_id"] or "",
                    row["status"] or "",
                    row["imported_at"],
                ),
            )

    def load_selected_person(self, _event: object) -> None:
        selected = self.people_tree.selection()
        if not selected:
            return
        values = self.people_tree.item(selected[0], "values")
        if not values:
            return
        self.uid_var.set(values[0])
        self.sid_var.set(values[1])
        self.name_var.set(values[2])
        self.department_var.set(values[3])
        self.card_type_var.set({"普通": "普通卡", "图像": "图像卡", "管理员": "管理员卡"}.get(values[4], "图像卡"))
        self.points_var.set(values[5])
        self.update_previews()

    def set_selected_lost(self, lost: bool) -> None:
        selected = self.people_tree.selection()
        if not selected:
            messagebox.showwarning(APP_TITLE, "请选择人员")
            return
        uid = self.people_tree.item(selected[0], "values")[0]
        self.db.mark_lost(uid, lost)
        self.refresh_people()

    def export_records(self) -> None:
        path = filedialog.asksaveasfilename(
            title="导出考勤记录",
            defaultextension=".csv",
            filetypes=[("CSV 文件", "*.csv")],
        )
        if not path:
            return
        self.db.export_attendance_csv(path)
        self.status_var.set(f"已导出：{path}")

    def on_close(self) -> None:
        self.client.close()
        self.db.close()
        self.destroy()


def main() -> None:
    app = AttendanceApp()
    app.mainloop()


if __name__ == "__main__":
    main()
