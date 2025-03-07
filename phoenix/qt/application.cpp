namespace phoenix {

XlibDisplay* pApplication::display = nullptr;

void pApplication::run() {
  if(Application::main) {
    while(applicationState.quit == false) {
      processEvents();
      Application::main();
    }
  } else {
    QApplication::exec();
  }
}

bool pApplication::pendingEvents() {

  QElapsedTimer timer;
  timer.start();

  QAbstractEventDispatcher* dispatcher = QCoreApplication::eventDispatcher();

  while (dispatcher->hasPendingEvents()) {
      if (timer.elapsed() > 100) break;
      QCoreApplication::processEvents();
      SDL_PumpEvents();
  }
  return dispatcher->hasPendingEvents();
}

void pApplication::processEvents() {
  while(pendingEvents()) QApplication::processEvents();
}

void pApplication::quit() {
  QApplication::quit();
  //note: QApplication cannot be deleted; or libQtGui will crash
  qtApplication = nullptr;
}

void pApplication::syncX() {
  for(unsigned n = 0; n < 8; n++) {
    SDL_PumpEvents();
    Application::processEvents();
    usleep(2000);
  }
}

void pApplication::initialize() {
  display = XOpenDisplay(0);

  if (!display) {
      fprintf(stderr, "Failed to initialize X Display for QApplication\n");
      exit(1);
  }

  settings = new Settings;
  settings->load();

  static int argc = 1;
  static char* argv[] = {new char[8], nullptr};
  strcpy(argv[0], "phoenix");
  char** argvp = argv;

  qtApplication = new QApplication(argc, argvp);

  pKeyboard::initialize();
}

}
