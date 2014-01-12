#include <misc.hpp>
#include <memblock.hpp>
#include <file.hpp>
#include <cmdline.hpp>
#undef bound
#include <stdlib.h>
#include <unistd.h>
#define QT_NO_STL
#include <qapplication.h>
#include <qclipboard.h>
#include <qdesktopwidget.h>
#include <q3filedialog.h>
#include <qmessagebox.h>
#include <q3listbox.h>
#include <q3accel.h>
#include <q3popupmenu.h>
#include <qmenubar.h>
#include <q3mainwindow.h>
#include <q3process.h>
#include <qsettings.h>
#include <q3scrollview.h>
#include <qscrollbar.h>
#include <qcolor.h>
#include <qpainter.h>

#include <sys/types.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define MESSAGE_BOX_CAPTION "aide"
#define SETTINGS_PREFIX	"/aide/"

#define NR_OF_MAKEFILES_TO_REMEMBER		8

class main_window_t;

class process_t: public Q3Process {
	Q_OBJECT

	STR pending_output;

	void process_pending_output(const uint accept_only_full_lines)
		{
			while (1) {
				const char * const buf=pending_output;
				if (!*buf)
					break;

				const char *line_end=strchr(buf,'\n');
				if (line_end == NULL) {

					if (accept_only_full_lines)
						break;

					line_end=strchr(buf,'\0');
					}

				const uint total_line_length=line_end + 1 - buf;
				if (line_end > buf && line_end[-1] == '\r')
					line_end--;

				MEMBLOCK line;
				line.AppendMem(buf,line_end - buf);
				line.AppendMem("",1);
				emit new_output_line((const char *)line.Ptr);

				pending_output.Delete(0,total_line_length);
				}
			}

	private slots:

	void read_from_stdout(void)
		{
			const QString data=readStdout();
			pending_output+=data.latin1();
			process_pending_output(1);
			}

	void process_exited(void)
		{
			process_pending_output(0);
			emit process_exited(normalExit() ? exitStatus() : -1);
			}

	signals:

	void new_output_line(const char * const line);
	void process_exited(const sint retval);

	public:

	process_t(main_window_t * const main_window,
				const char * const command_name,
				const uint want_output=0,
				const char * const fname=NULL,
				const uint line_nr=0);

	uint run(void)
		{
			if (launch(QString("")))
				return 1;

			printf("Failed to start sh\n");
			return 0;
			}
	};

class aide_application_t : public QApplication {

	const Window x11_root_window;
	Atom request_atom,exists_atom;
	uint IPC_atoms_set;		// 0 or 1

	main_window_t *main_window;

	void try_to_set_IPC_atoms(void);
	uint we_have_latest_EXISTS(void) const;
	static uint window_title_matches_fname(
				const char * const title,const char * const fname);

	protected:

	virtual bool x11EventFilter(XEvent *e);

	public:

	Display * const display;

	aide_application_t(sint argc,char **argv) :
			QApplication(argc,argv),
			x11_root_window(RootWindow(desktop()->x11Display(),
								DefaultScreen(desktop()->x11Display()))),
			IPC_atoms_set(0), main_window(NULL),
			display(desktop()->x11Display())
		{
			XSelectInput(display,x11_root_window,PropertyChangeMask);
			try_to_set_IPC_atoms();
			}

	void set_main_window(main_window_t * const mw);
	void set_IPC_EXISTS(void) const;

	uint focus_editor_window(
			const char * const fname,const Window subtree_root=0,
									const uint tree_level=0) const;

	void send_IPC_request(const char * const str)
		{
			if (!IPC_atoms_set)
				return;

			XChangeProperty(display,x11_root_window,
					request_atom,XA_STRING,8,PropModeReplace,
					(uchar *)str,strlen(str));
			}

	~aide_application_t(void)
		{
			if (we_have_latest_EXISTS()) {
				XDeleteProperty(display,x11_root_window,exists_atom);
				XSync(display,True);
				}
			}
	};

class compile_window_t : public Q3ListBox {
	Q_OBJECT

	class output_line_t : public Q3ListBoxText {
		QColor color;

		protected:

		void paint(QPainter *painter)
			{
				if (isCurrent())
					Q3ListBoxText::paint(painter);
				  else {
					const QPen &prev_pen=painter->pen();
					painter->setPen(color);
					Q3ListBoxText::paint(painter);
					painter->setPen(prev_pen);
					}
				}

		public:

		output_line_t(const QString &str,const QColor &_color) :
								Q3ListBoxText(str), color(_color) {}
		};

	main_window_t * const main_window;
	process_t *make_process;		// NULL if no make process running

	uint is_scrolling_to_end;		// 0 or 1
	uint last_line_was_errorwarning;

	enum line_type_t {NORMAL_LINE=0,ERRORWARNING_LINE,CONTINUATION_LINE};

	static line_type_t decode_output_line(const char * const str,
											uint &fname_len,uint &line_nr)
		{	// if return value is ERRORWARNING_LINE, then also sets
			//   fname_len and line_nr to nonzero

			fname_len=0;
			line_nr=0;

			if (*str == ' ')
				return CONTINUATION_LINE;

			const char *p=str;
			while (((uchar)*p) > ' ' && *p != ':' && *p != '(')
				p++;

			fname_len=p - str;

			if (!fname_len)
				return NORMAL_LINE;

			if (*p != ':' && *p != '(')
				return NORMAL_LINE;

			const sint line_num=atoi(p+1);
			if (line_num <= 0)
				return NORMAL_LINE;

			line_nr=(uint)line_num;
			return ERRORWARNING_LINE;
			}

	uint find_continuation_line_start(const uint idx)
		{
			uint i=idx;
			uint fname_len,line_nr;
			while (decode_output_line(text(i),fname_len,line_nr) ==
														CONTINUATION_LINE) {
				if (!i)
					return idx;
				i--;
				}

			return i;
			}

	void start_making(const char * const command_name,
									const char * const srcfile=NULL);
	public slots:

	void start_editing_error(int idx);
	void start_making_all(void) { start_making("makeall"); }
	void start_making_clean(void) { start_making("makeclean"); }
	void start_making_clean_all(void) { start_making("makecleanall"); }
	void start_compiling_one_file(const char * const source_file_name);

	void stop_scrolling_to_end(void) {
		if (is_scrolling_to_end) {
			clearSelection();
			is_scrolling_to_end=0;
			}
		}

	void append_output_line(const char * const line)
		{
			uint fname_len,line_nr;
			switch (decode_output_line(line,fname_len,line_nr)) {
				case NORMAL_LINE:		last_line_was_errorwarning=0;
										break;
				case ERRORWARNING_LINE:	last_line_was_errorwarning=1;
										break;
				case CONTINUATION_LINE:	break;
				}

			Q3ListBoxItem * const new_item=new output_line_t(line,
							last_line_was_errorwarning ? Qt::red : Qt::black);
			insertItem(new_item);

			if (is_scrolling_to_end) {
				const Q3ListBoxItem * const cur_item=item(currentItem());
				if (cur_item == NULL)
					setCurrentItem(new_item);
				  else
					if (cur_item->next() == NULL)
						setCurrentItem(new_item);
				}
			}

	void make_process_exited(const sint retval)
		{
			if (make_process != NULL) {
				const char * result=retval ? "Make failed" : "Make successful";
				append_output_line(result);
				setCaption(result);

				removeChild(make_process);
				delete make_process;
				make_process=NULL;
				}
			}

	void copy_line(void)
		{
			sint idx=currentItem();
			if (idx < 0)
				return;

			idx=(sint)find_continuation_line_start((uint)idx);

			QString str=text(idx).stripWhiteSpace();
			idx++;
			while (1) {
				const QString s=text(idx);
				if (s == QString::null)
					break;

				uint fname_len,line_nr;
				if (decode_output_line(s,fname_len,line_nr) !=
															CONTINUATION_LINE)
					break;

				str.append(" ");
				str.append(s.stripWhiteSpace());
				idx++;
				}

			QApplication::clipboard()->setText(str,QClipboard::Clipboard);
			}

	void copy_all_errors(void)
		{
			QString str;
			line_type_t last_line_type=NORMAL_LINE;

			for (sint idx=0;;idx++) {
				QString s=text(idx);
				if (s == QString::null)
					break;

				uint fname_len,line_nr;
				const line_type_t lowlevel_line_type=decode_output_line(
													s,fname_len,line_nr);
				line_type_t line_type=lowlevel_line_type;
				if (line_type == CONTINUATION_LINE)
					line_type=last_line_type;
				  else
					last_line_type=line_type;

				if (line_type == ERRORWARNING_LINE) {
					if (!str.isEmpty())
						str.append((lowlevel_line_type ==
										CONTINUATION_LINE) ? " " : "\n");
					str.append(s.stripWhiteSpace());
					}
				}

			QApplication::clipboard()->setText(str,QClipboard::Clipboard);
			}

	protected:

	virtual void moveEvent(QMoveEvent *e);
	virtual void resizeEvent(QResizeEvent *e);

	public:

	compile_window_t(main_window_t * const _main_window);
	};

class main_window_t : public Q3MainWindow {
	Q_OBJECT

	compile_window_t compile_window;
	Q3ListBox sources_list;
	Q3PopupMenu file_menu;
	process_t *getsources_process;	// NULL if no getsources process running

	void set_recent_makefiles_in_file_menu(void);
	static QString get_window_key_prefix(const QWidget * const w);

	public:

	STR last_compiled_source_fname;

	public slots:

	void start_editing_file(const QString &_fname)
		{
			const char * const fname=_fname.latin1();

			process_t * const process=new process_t(this,"edit",0,fname);
			process->run();
			app->focus_editor_window(fname);

			app->set_IPC_EXISTS();

			if (strcmp(makefile_fname,fname))
				last_compiled_source_fname=fname;
			}

	void start_compiling_current_file(void)
		{
			compile_window.start_compiling_one_file(
							sources_list.currentText().latin1());
			}

	void load_makefile(const QString &fname);

	void start_editing_makefile(void)
		{
			if (*makefile_fname)
				start_editing_file((const char *)makefile_fname);
			}

	void open_file_dialog(void)
		{
			const QString fname=Q3FileDialog::getOpenFileName(
					QString::null,"makefiles (Makefile* *.mk)",
					this,"open makefile dialog","Open makefile");
			if (fname.isNull())
				return;

			load_makefile(fname);
			}

	void load_recent_makefile(int menuitem_id)
		{
			if (menuitem_id <= 0)
				return;

			QString menuitem_text=file_menu.text(menuitem_id);
			while (menuitem_text.length() && menuitem_text.at(0) != ' ')
				menuitem_text.remove(0,1);

			load_makefile(menuitem_text.stripWhiteSpace());
			}

	void load_last_makefile(void)
		{
			load_makefile(settings.readEntry(
							SETTINGS_PREFIX "recent_makefiles/1"));
			}

	void add_source_file(const char * const fname)
		{
			if (sources_list.findItem(fname,
					Q3ListBox::CaseSensitive|Q3ListBox::ExactMatch) == NULL) {
				sources_list.insertItem(fname);
				sources_list.sort();
				last_compiled_source_fname=fname;
				}
			}

	void getsources_process_exited(const sint retval)
		{
			if (getsources_process == NULL)
				return;

			if (retval) {
				char buf[300];
				sprintf(buf,"getsources process failed (retval=%d)",retval);

				QMessageBox::warning(this,MESSAGE_BOX_CAPTION,buf,
						QMessageBox::Ok,QMessageBox::NoButton);
				}

			removeChild(getsources_process);
			delete getsources_process;
			getsources_process=NULL;
			}

	protected:

	virtual void moveEvent(QMoveEvent *e);
	virtual void resizeEvent(QResizeEvent *e);

	public:

	filename makefile_fname;		// without directory
	QSettings settings;
	const aide_application_t * const app;

	void IPC_request(const char * const str)
		{
			const char *source_file_name=last_compiled_source_fname;

			const char *separator=strchr(str,':');
			if (separator == NULL)
				separator=strchr(str,'\0');
			  else {
				const filename fname(separator + 1);
				const char * const basename=fname.BaseName();

				for (const Q3ListBoxItem *qlbi=sources_list.firstItem();
									qlbi != NULL;qlbi=qlbi->next()) {
					QString txt=qlbi->text();
					if (txt.isNull())
						continue;

					const filename item_fname(txt);
					if (!strcmp(basename,item_fname.BaseName())) {
						source_file_name=txt.latin1();
						break;
						}
					}
				}

			const uint command_len=separator - str;
			if (!command_len)
				return;

			const STR command(str,command_len);

			if (!strcmp(command,"build"))
				compile_window.start_making_all();
			else
			if (!strcmp(command,"rebuild"))
				compile_window.start_making_clean_all();
			else
			if (!strcmp(command,"compile") && source_file_name != NULL)
				compile_window.start_compiling_one_file(
												STR(source_file_name));
			}

	void load_window_pos(QWidget * const w);
	void save_window_pos(const QWidget * const w);
	void save_window_size(const QWidget * const w);
	QString get_command_by_name(const char * const command_name);

	main_window_t(const aide_application_t * const _app);
	};

/***************************************************************************/
/***************************                    ****************************/
/*************************** compile_window_t:: ****************************/
/***************************                    ****************************/
/***************************************************************************/

compile_window_t::compile_window_t(main_window_t * const _main_window) :
					Q3ListBox(NULL,"compile_window"),
					main_window(_main_window), make_process(NULL)
{
	setFrameStyle(NoFrame);

	connect(verticalScrollBar(),SIGNAL(sliderMoved(int)),
									SLOT(stop_scrolling_to_end()));

	connect(this,SIGNAL(selected(int)),SLOT(start_editing_error(int)));

	Q3Accel * const accel=new Q3Accel(this);
	accel->connectItem(accel->insertItem(Qt::CTRL + Qt::Key_C),
										this,SLOT(copy_line(void)));
	accel->connectItem(accel->insertItem(Qt::CTRL + Qt::SHIFT + Qt::Key_C),
										this,SLOT(copy_all_errors(void)));
	}

void compile_window_t::moveEvent(QMoveEvent *e)
{
	Q3ListBox::moveEvent(e);
	main_window->save_window_pos(this);
	}

void compile_window_t::resizeEvent(QResizeEvent *e)
{
	Q3ListBox::resizeEvent(e);
	main_window->save_window_size(this);
	}

void compile_window_t::start_making(const char * const command_name,
										const char * const srcfile)
{
	main_window->app->set_IPC_EXISTS();

	is_scrolling_to_end=1;
	last_line_was_errorwarning=0;

	if (!isVisible())
		main_window->load_window_pos(this);

	show();
	setActiveWindow();
	setFocus();
	raise();

	if (make_process != NULL)
		return;		//!!! display an error message like wide does?

	clear();
	make_process=new process_t(main_window,command_name,1,srcfile);

	connect(make_process,SIGNAL(process_exited(const sint)),
	                SLOT(make_process_exited(const sint)));
	connect(make_process,SIGNAL(new_output_line(const char * const)),
	                SLOT(append_output_line(const char * const)));

	if (!make_process->run()) {
		removeChild(make_process);
		delete make_process;
		make_process=NULL;
		setCaption("Error running make process");
		}
	  else
		setCaption("Running make...");
	}

void compile_window_t::start_compiling_one_file(
									const char * const source_file_name)
{
	start_making("makeone",source_file_name);
	main_window->last_compiled_source_fname=source_file_name;
	}

void compile_window_t::start_editing_error(int idx)
{
	if (idx < 0)
		return;

	idx=(sint)find_continuation_line_start((uint)idx);

	const char * const line=text(idx).latin1();

	uint fname_len,line_nr;
	if (decode_output_line(line,fname_len,line_nr) != ERRORWARNING_LINE)
		return;

	STR fname(line,fname_len);

	process_t * const process=new process_t(main_window,
										"editline",0,fname,line_nr);
	process->run();

	main_window->app->focus_editor_window(fname);
	}

process_t::process_t(main_window_t * const main_window,
					const char * const command_name,
					const uint want_output,
					const char * const fname,
					const uint line_nr) :
						Q3Process(main_window,command_name)
{
	setCommunication(want_output ? (Stdout|Stderr|DupStderr) : 0);

	addArgument("/bin/sh");
	addArgument("-c");

	QString src=main_window->get_command_by_name(command_name);
	QString dest;

	while (src.length()) {
		const QChar chr=src.at(0);
		src.remove(0,1);

		if (chr != '%') {
			dest.append(chr);
			continue;
			}

		if (src.startsWith("%")) {
			dest.append(src.at(0));
			src.remove(0,1);
			continue;
			}

		if (src.startsWith("makefile")) {
			src.remove(0,8);
			dest.append((const char *)main_window->makefile_fname);
			continue;
			}

		if (src.startsWith("file")) {
			src.remove(0,4);
			if (fname != NULL)
				dest.append(fname);
			continue;
			}

		if (src.startsWith("line")) {
			src.remove(0,4);
			dest.append(QString::number(line_nr));
			continue;
			}

		dest.append(chr);
		}

	addArgument(dest);

	connect(this,SIGNAL(readyReadStdout()),SLOT(read_from_stdout()));
	connect(this,SIGNAL(processExited()),SLOT(process_exited()));
	}

/***************************************************************************/
/*****************************                 *****************************/
/***************************** main_window_t:: *****************************/
/*****************************                 *****************************/
/***************************************************************************/

main_window_t::main_window_t(const aide_application_t * const _app) :
					Q3MainWindow(NULL,"main_window"),
					compile_window(this), sources_list(this),
					file_menu(this), getsources_process(NULL),
					last_compiled_source_fname(NULL), app(_app)
{
	sources_list.setFrameStyle(Q3Frame::NoFrame);
	setCentralWidget(&sources_list);

	connect(&sources_list,SIGNAL(selected(const QString &)),
						SLOT(start_editing_file(const QString &)));

	menuBar()->insertItem("&File",&file_menu);

	file_menu.insertItem("&Open makefile..",
					this,SLOT(open_file_dialog()),Qt::CTRL + Qt::Key_O);
	file_menu.insertItem("&Edit makefile",
					this,SLOT(start_editing_makefile()),Qt::CTRL + Qt::Key_E);
	file_menu.insertItem("E&xit",
					app,SLOT(quit()),Qt::CTRL + Qt::Key_Q);
	file_menu.insertSeparator();

	set_recent_makefiles_in_file_menu();
	connect(&file_menu,SIGNAL(activated(int)),
									SLOT(load_recent_makefile(int)));


	{Q3PopupMenu * const menu=new Q3PopupMenu(this);
	menuBar()->insertItem("&Build",menu);

	menu->insertItem("&Compile current source",
					this,SLOT(start_compiling_current_file()),Qt::Key_F3);
	menu->insertItem("&Make all",
					&compile_window,SLOT(start_making_all()),Qt::Key_F4);
	menu->insertItem("Make clean &&&& &all",
					&compile_window,SLOT(start_making_clean_all()),Qt::Key_F5);
	menu->insertItem("Make &clean",
					&compile_window,SLOT(start_making_clean()));}
	}

void main_window_t::set_recent_makefiles_in_file_menu(void)
{
	for (uint i=1;i <= NR_OF_MAKEFILES_TO_REMEMBER;i++) {
		if (file_menu.indexOf((sint)i) >= 0)
			file_menu.removeItem((sint)i);

		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_makefiles/%u",i);

		QString entry=settings.readEntry(settings_key);
		if (entry.isNull())
			continue;

		entry.prepend(' ');
		entry.prepend(QString::number(i));
		entry.prepend('&');
		file_menu.insertItem(entry,(sint)i);
		}
	}

void main_window_t::moveEvent(QMoveEvent *e)
{
	Q3MainWindow::moveEvent(e);
	save_window_pos(this);
	}

void main_window_t::resizeEvent(QResizeEvent *e)
{
	Q3MainWindow::resizeEvent(e);
	save_window_size(this);
	}

QString main_window_t::get_command_by_name(const char * const command_name)
{
	char settings_key[500];
	sprintf(settings_key,SETTINGS_PREFIX "command_lines/%s",command_name);

	const char *default_command=NULL;

	if (!strcmp(command_name,"makeall"))
		default_command="make -j2 -f %makefile";
	else
	if (!strcmp(command_name,"makeclean"))
		default_command="make -f %makefile clean";
	else
	if (!strcmp(command_name,"makecleanall"))
		default_command="make -f %makefile clean && make -j2 -f %makefile";
	else
	if (!strcmp(command_name,"makeone"))
		default_command="COMPILE_TARGETS=`make -f %makefile -qp | "
			"egrep ': *[^ :%]*%file($| )' | "
			"sed -e 's/:.*//' | "
			"grep -Evw ^\\`make -f %makefile -qp | "
				"grep '^.PHONY:' | "
				"tr ' ' '|' | "
				"sed -e 's/||*$/$/' -e 's/|/$|^/g' -e 's/$/$/'\\` |"
			"tr '\n' ' '`; "
			"echo Making $COMPILE_TARGETS ; "
			"touch -d '19700101 UTC' $COMPILE_TARGETS ; "
			"make -f %makefile $COMPILE_TARGETS";
	else
	if (!strcmp(command_name,"getsources"))
		default_command="make -f %makefile -qp | "
					"grep '^[^ %:.][^ %:]* *: *[^ %:=][^ %:]*' | "
					"sed -e 's/^.*: *//' -e 's/ .*$//' | "
					"grep -Ev ^`make -f %makefile -qp | "
						"grep '^[^ %:][^ %:]* *: *[^ %:]' | "
						"sed -e 's/^\\([^.].*\\):.*/\\1/' | "
						"tr ' \n' '||' |"
						"sed -e 's/||*$/$/' -e 's/|/$|^/g'`";
	else
	if (!strcmp(command_name,"edit"))
		default_command="nedit-client -noask %file";
	else
	if (!strcmp(command_name,"editline"))
		default_command="nedit-client -noask -line %line %file";

	return settings.readEntry(settings_key,default_command);
	}

QString main_window_t::get_window_key_prefix(const QWidget * const w)
{
	const char * const window_name=w->name();
	if (window_name == NULL)
		return QString::null;
	if (!*window_name || !strcmp(window_name,"unnamed"))
		return QString::null;

	return QString(SETTINGS_PREFIX) + window_name;
	}

void main_window_t::save_window_pos(const QWidget * const w)
{
	if (!w->isVisible() || !w->isActiveWindow())
		return;

	const QString key_prefix=get_window_key_prefix(w);
	if (key_prefix.isNull())
		return;

	settings.writeEntry(key_prefix +  "/pos/x",w->pos().x());
	settings.writeEntry(key_prefix +  "/pos/y",w->pos().y());
	}

void main_window_t::save_window_size(const QWidget * const w)
{
	if (!w->isVisible() || !w->isActiveWindow())
		return;

	const QString key_prefix=get_window_key_prefix(w);
	if (key_prefix.isNull())
		return;

	settings.writeEntry(key_prefix + "/size/x",w->size().width());
	settings.writeEntry(key_prefix + "/size/y",w->size().height());
	}

void main_window_t::load_window_pos(QWidget * const w)
{
	const QString key_prefix=get_window_key_prefix(w);
	if (key_prefix.isNull())
		return;

	bool pos_x_ok=(bool)0;
	const sint pos_x=settings.readNumEntry(key_prefix + "/pos/x",0,&pos_x_ok);
	bool pos_y_ok=(bool)0;
	const sint pos_y=settings.readNumEntry(key_prefix + "/pos/y",0,&pos_y_ok);
	if (pos_x_ok && pos_y_ok)
		w->move(QPoint(pos_x,pos_y));

	const sint size_x=settings.readNumEntry(key_prefix + "/size/x");
	const sint size_y=settings.readNumEntry(key_prefix + "/size/y");
	if (size_x > 0 && size_y > 0)
		w->resize(QSize(size_x,size_y));
	}

void main_window_t::load_makefile(const QString &fname_string)
{
	if (fname_string.isNull())
		return;

	app->set_IPC_EXISTS();

	const filename full_fname(fname_string.latin1());

	filename dirname=full_fname;
	dirname.StripBaseName();
	if (*dirname && chdir(dirname) != 0) {
		char buf[500];
		sprintf(buf,"Path %s not found",(const char *)dirname);
		QMessageBox::warning(this,MESSAGE_BOX_CAPTION,buf,
						QMessageBox::Ok,QMessageBox::NoButton);
		return;
		}

	const char * const fname=full_fname.BaseName();
	if (!*fname || !file::Exists(fname)) {
		char buf[800];
		sprintf(buf,"File %s not found",(const char *)full_fname);
		QMessageBox::warning(this,MESSAGE_BOX_CAPTION,buf,
						QMessageBox::Ok,QMessageBox::NoButton);
		return;
		}

		// update recent makefiles list

	{QString recent_makefiles[NR_OF_MAKEFILES_TO_REMEMBER];
	recent_makefiles[0]=QFileInfo(QString(fname)).absFilePath();
	uint nr_of_recent_makefiles=1;

	uint i;
	for (i=1;i <= NR_OF_MAKEFILES_TO_REMEMBER &&
				nr_of_recent_makefiles < NR_OF_MAKEFILES_TO_REMEMBER;i++) {
		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_makefiles/%u",i);

		const QString entry=settings.readEntry(settings_key);
		if (entry.isNull())
			continue;

		if (entry == recent_makefiles[0])
			continue;

		recent_makefiles[nr_of_recent_makefiles++]=entry;
		}

	for (i=0;i < nr_of_recent_makefiles;i++) {
		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_makefiles/%u",i + 1);

		settings.writeEntry(settings_key,recent_makefiles[i]);
		}

	set_recent_makefiles_in_file_menu();}


	makefile_fname.Set(fname);
	sources_list.clear();

	getsources_process=new process_t(this,"getsources",1);

	connect(getsources_process,SIGNAL(process_exited(const sint)),
	                SLOT(getsources_process_exited(const sint)));
	connect(getsources_process,SIGNAL(new_output_line(const char * const)),
	                SLOT(add_source_file(const char * const)));

	if (!getsources_process->run()) {
		removeChild(getsources_process);
		delete getsources_process;
		getsources_process=NULL;
		}
	}

/***************************************************************************/
/**************************                      ***************************/
/************************** aide_application_t:: ***************************/
/**************************                      ***************************/
/***************************************************************************/

uint aide_application_t::window_title_matches_fname(
				const char * const title,const char * const fname)
{
	const char * const p=strstr(title,fname);
	if (p == NULL)
		return 0;

	if (p > title) {
		const char chr=p[-1];
		if (chr >= 'a' && chr <= 'z')
			return 0;
		if (chr >= 'A' && chr <= 'Z')
			return 0;
		if (chr >= '0' && chr <= '9')
			return 0;
		}

	const uint fname_len=strlen(fname);
	const char end_chr=p[fname_len];
	if (end_chr != '\0') {
		if (end_chr >= 'a' && end_chr <= 'z')
			return 0;
		if (end_chr >= 'A' && end_chr <= 'Z')
			return 0;
		if (end_chr >= '0' && end_chr <= '9')
			return 0;
		}

	return 1;
	}

uint aide_application_t::focus_editor_window(
			const char * const fname,const Window subtree_root,
									const uint tree_level) const
{
	const Window w=subtree_root ? subtree_root :x11_root_window;

	Window root_window,parent_window;
	Window *children_list;
	uint nr_of_children;
	if (!XQueryTree(display,w,&root_window,&parent_window,
							&children_list,&nr_of_children)) {
		fprintf(stderr,"XQueryTree() failed\n");
		return 0;
		}

 	if (children_list == NULL || !nr_of_children)
		return 0;


	const filename fn(fname);
	const char * const basename=fn.BaseName();

	uint result=0;
	uint i;
	for (i=0;i < nr_of_children && !result;i++) {
		XWindowAttributes attr;
		if (!XGetWindowAttributes(display,children_list[i],&attr))
			continue;

		if (attr.map_state != IsViewable)
			continue;

		char *name=NULL;
		if (!XFetchName(display,children_list[i],&name))
			name=NULL;

		if (name == NULL)
			continue;

		result=window_title_matches_fname(name,basename);

		if (result) {
			XRaiseWindow(display,children_list[i]);
			XSetInputFocus(display,children_list[i],
									RevertToParent,CurrentTime);
			}

		XFree(name);
		}

	for (i=0;i < nr_of_children && !result && tree_level+1 < 3;i++)
		result=focus_editor_window(basename,children_list[i],
												tree_level+1);

	XFree(children_list);
	return result;
	}

void aide_application_t::try_to_set_IPC_atoms(void)
{
		// set username

	const struct passwd * const passwd_entry=getpwuid(getuid());
	if (passwd_entry == NULL) {
		printf("aide: getpwuid() failed, IPC disabled\n");
		return;
		}

	const char * const username=passwd_entry->pw_name;

		// set hostname

	struct utsname name_struct;
	if (uname(&name_struct) < 0) {
		printf("aide: uname() failed, IPC disabled\n");
		return;
		}
	const STR hostname(name_struct.nodename);

		// set request_atom

	{STR property_name;
	property_name+="AIDE_REQUEST_";
	property_name+=hostname;
	property_name+="_";
	property_name+=username;

	request_atom=XInternAtom(display,
							(const char *)property_name,False);}

		// set exists_atom

	{STR property_name;
	property_name+="AIDE_EXISTS_";
	property_name+=hostname;
	property_name+="_";
	property_name+=username;

	exists_atom=XInternAtom(display,
							(const char *)property_name,False);}

	IPC_atoms_set=1;
	}

void aide_application_t::set_IPC_EXISTS(void) const
{
	if (main_window == NULL || !IPC_atoms_set)
		return;

	const Window w=main_window->handle();

	XChangeProperty(display,x11_root_window,
					exists_atom,XA_WINDOW,sizeof(Window)*8 /* 32 */,
					PropModeReplace,
					(uchar *)&w,1);
	}

uint aide_application_t::we_have_latest_EXISTS(void) const
{
	if (main_window == NULL || !IPC_atoms_set)
		return 0;

	Atom dummy_atom;
	ulong nr_of_items,dummy_ulong;
	uchar *property_value;
	sint format;

	if (XGetWindowProperty(display,x11_root_window,
						exists_atom,0,INT_MAX,False,
						XA_WINDOW,&dummy_atom,&format,&nr_of_items,
						&dummy_ulong,&property_value) != Success)
		return 1;

	uint return_value=1;
	if (format == sizeof(Window)*8 && nr_of_items) {
		if (main_window->handle() != *(const Window *)property_value)
			return_value=0;
		}

	XFree(property_value);
	return return_value;
	}

bool aide_application_t::x11EventFilter(XEvent *e)
{
	if (e->type == PropertyNotify && IPC_atoms_set) {
		const XPropertyEvent * const pe=(const XPropertyEvent *)e;

		if (pe->window == x11_root_window &&
				pe->atom == request_atom &&
				pe->state == PropertyNewValue &&
				main_window != NULL) {

			if (we_have_latest_EXISTS()) {
				Atom dummy_atom;
				ulong nr_of_items,dummy_ulong;
				uchar *property_value;
				sint format;

				if (XGetWindowProperty(display,x11_root_window,
						request_atom,0,INT_MAX,True,
						XA_STRING,&dummy_atom,&format,&nr_of_items,
						&dummy_ulong,&property_value) == Success) {

					if (format == 8) {
						MEMBLOCK mb;
						mb.AppendMem(property_value,nr_of_items);
						mb.AppendMem("",1);

						main_window->IPC_request((const char *)mb.Ptr);
						}

					XFree(property_value);
					}
				}
			}
		}

	return FALSE;
	}

void aide_application_t::set_main_window(main_window_t * const mw)
{
	main_window=mw;
	setMainWidget(mw);
	}

int main(sint argc,char **argv)
{
	QApplication::setOrganizationName("aide");
	QApplication::setApplicationName("aide");
	aide_application_t a(argc,argv);

	cmdline::init(argc,argv);

	if (cmdline::is_present("compile")) {
		const char * const fname=cmdline::get_str_switch("compile");
		a.send_IPC_request((fname == NULL) ? "compile" :
										(STR("compile:") << fname));
		// a.unlock();
		return 0;
		}

	if (cmdline::is_present("build")) {
		a.send_IPC_request("build");
		// a.unlock();
		return 0;
		}

	main_window_t main_window(&a);
	a.set_main_window(&main_window);
	main_window.load_window_pos(&main_window);
	main_window.show();

	if (cmdline::is_present("openlast"))
		main_window.load_last_makefile();
	  else
		if (file::Exists("Makefile"))
			main_window.load_makefile("Makefile");

	return a.exec();
	}

#include "main.moc"
