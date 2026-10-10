class TestPlayback {
public:
 bool m_playbackVisible=true;
 CButton m_playbackStartTargetButton,m_playbackTargetEndButton,m_playbackResetButton,m_playbackPlayButton,m_playbackStopButton,m_playbackStepBackButton,m_playbackStepForwardButton,m_playbackSpeedButton;
 CProgressCtrl m_playbackProgress;CStatic m_playbackCounter;
 void LayoutPlaybackUi(int,int,int);
 std::vector<CWnd*> controls(){return {&m_playbackStartTargetButton,&m_playbackTargetEndButton,&m_playbackResetButton,&m_playbackPlayButton,&m_playbackStopButton,&m_playbackStepBackButton,&m_playbackStepForwardButton,&m_playbackSpeedButton,&m_playbackProgress,&m_playbackCounter};}
};
// PRODUCTION_LAYOUT
void playbackTest(){
 CWnd host;CHECK(host.CreateEx(0,AfxRegisterWndClass(0),L"Player layout test",WS_OVERLAPPEDWINDOW,CRect(-2000,-2000,-1300,-1600),nullptr,0));
 TestPlayback playback;int id=201;for(auto* c:playback.controls())CHECK(c->CreateEx(0,L"BUTTON",L"player",WS_CHILD|WS_VISIBLE,CRect(0,0,20,20),&host,id++));
 playback.LayoutPlaybackUi(600,20,28);CHECK(playback.m_playbackCounter.GetStyle()&WS_VISIBLE);
 playback.LayoutPlaybackUi(200,50,28);CHECK(!(playback.m_playbackCounter.GetStyle()&WS_VISIBLE));
 int previous=0;for(auto* c:playback.controls()){if(!(c->GetStyle()&WS_VISIBLE))continue;auto rect=localRect(host,*c);CHECK(rect.left>=previous);CHECK(rect.right<=200);CHECK(rect.top>=50);previous=rect.right;}
 playback.LayoutPlaybackUi(600,20,28);CHECK(playback.m_playbackCounter.GetStyle()&WS_VISIBLE);
 for(auto* c:playback.controls())c->DestroyWindow();host.DestroyWindow();
}
