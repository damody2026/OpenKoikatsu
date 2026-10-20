// The books (Docs/STREET_ACCOUNTING_PLAN_20261012.md). Included by OKStreetSocialScene.cpp before every file that
// moves money. The money rules change an account through the helpers here (OKCash, OKBank, OKPrivateTake, the company
// wallet), which also leave a post on the store; TickBooks writes the posts of the store in effect to the journal and
// checks every account against it, so a change that came without a post is still recorded ("未分類", with a warning).

// A post for the journal (no change to the account itself).
void OKBookPost(FOKStreetStore& C,const FString& Book,const FString& Account,int64 Amount,const FString& Category,const FString& Note){
 if(Amount==0)return;FOKBookPost P;P.Serial=OKBookNextSerial();P.Book=Book;P.Account=Account;P.Amount=Amount;P.Category=Category;P.Note=Note;C.BookPending.Add(MoveTemp(P));
}
// His wallet and his savings account.
void OKCash(FOKStreetStore& C,int64 Amount,const FString& Category,const FString& Note){C.PlayerCash+=Amount;OKBookPost(C,TEXT("private"),TEXT("cash"),Amount,Category,Note);}
void OKBank(FOKStreetStore& C,int64 Amount,const FString& Category,const FString& Note){C.PlayerBank+=Amount;OKBookPost(C,TEXT("private"),TEXT("bank"),Amount,Category,Note);}
// A payment from his savings first, then the wallet; nothing is taken unless all of it can be.
bool OKPrivateTake(FOKStreetStore& C,int64 Amount,const FString& Category,const FString& Note){
 if(Amount<=0)return true;if(C.PlayerBank+C.PlayerCash<Amount)return false;
 const int64 FromBank=FMath::Min(C.PlayerBank,Amount);if(FromBank>0)OKBank(C,-FromBank,Category,Note);if(Amount>FromBank)OKCash(C,-(Amount-FromBank),Category,Note);return true;
}

// Every account of every book as it stands in a store (liabilities negative).
void OKBookBalances(const FOKStreetStore& C,TMap<FString,TMap<FString,int64>>& Out){
 Out.Reset();auto& P=Out.Add(TEXT("private"));const auto& B=C.Banking;
 P.Add(TEXT("cash"),C.PlayerCash);P.Add(TEXT("bank"),C.PlayerBank);P.Add(TEXT("deposit"),OKStreetDepositsTotal(B));P.Add(TEXT("card"),-B.Card.Balance);
 P.Add(TEXT("loan"),-B.Loan.Owed);P.Add(TEXT("arrears"),-B.PhoneArrears);
 int64 Lent=0;for(const auto& R:C.Relations)Lent+=R.OwesPlayer;P.Add(TEXT("lent"),Lent);P.Add(TEXT("owner_loan"),C.Company.Estate.OwnerLoan);
 auto& Co=Out.Add(TEXT("company"));Co.Add(TEXT("cash"),C.Company.Estate.Cash);Co.Add(TEXT("owner_loan"),-C.Company.Estate.OwnerLoan);
 for(const auto& H:C.HeroCompanies){auto& X=Out.Add(TEXT("hc_")+H.Owner);X.Add(TEXT("cash"),H.Estate.Cash);X.Add(TEXT("owner_loan"),-H.Estate.OwnerLoan);}
}

FString AOKStreetSocialScene::BookPath(const FString& Book) const{
 if(!State||State->SelfTest||State->Manifest.SavePath.IsEmpty()||Book.IsEmpty())return FString();
 return FPaths::GetPath(State->Manifest.SavePath)/TEXT("accounts")/(Book+TEXT(".jsonl"));
}

// A book's journal: read from its file the first time (the whole game), kept in memory after.
TArray<FOKBookEntry>& AOKStreetSocialScene::BookOf(const FString& Book){
 auto& S=*State;auto& List=S.Books.FindOrAdd(Book);
 if(!S.BooksLoaded.Contains(Book)){S.BooksLoaded.Add(Book);
  const FString Path=BookPath(Book);TArray<FString> Lines;
  if(!Path.IsEmpty()&&FFileHelper::LoadFileToStringArray(Lines,*Path)){TArray<FOKBookEntry> Loaded;Loaded.Reserve(Lines.Num());
   for(const FString& Line:Lines){FOKBookEntry E;if(OKBookEntryParse(Line,E))Loaded.Add(MoveTemp(E));}
   Loaded.Append(MoveTemp(List));List=MoveTemp(Loaded);}
 }
 return List;
}

void AOKStreetSocialScene::BookAppend(const FString& Book,FOKBookEntry&& E){
 auto& S=*State;if(E.Minute==0)E.Minute=OKStreetMindNow(S);if(E.Day==0)E.Day=S.Store.CalendarDay;
 const FString Path=BookPath(Book);
 if(!Path.IsEmpty())FFileHelper::SaveStringToFile(OKBookEntryLine(E)+TEXT("\n"),*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
 BookOf(Book).Add(MoveTemp(E));
}

// The last balance the journal knows for each account of a book.
static void OKBookLastBalances(const TArray<FOKBookEntry>& List,TMap<FString,int64>& Out){Out.Reset();for(const auto& E:List)Out.Add(E.Account,E.Balance);}

void AOKStreetSocialScene::TickBooks(){
 auto& S=*State;if(!S.AllReady)return;
 const double Wall=FPlatformTime::Seconds();if(Wall<S.BooksTickAt&&S.Store.BookPending.IsEmpty())return;S.BooksTickAt=Wall+0.25;
 TMap<FString,TMap<FString,int64>> Now;OKBookBalances(S.Store,Now);
 const int64 Floor=FDateTime::UtcNow().GetTicks()-ETimespan::TicksPerMinute*10;
 auto Fresh=[&](const FOKBookPost& P){return P.Serial>=Floor&&!S.BookFlushed.Contains(P.Serial);};
 // A book seen for the first time: the journal's last balances; an empty journal opens with today's (期初餘額),
 // before the posts still to come.
 auto Ensure=[&](const FString& Book){
  if(S.BookSeen.Contains(Book))return;auto& Seen=S.BookSeen.Add(Book);const auto& List=BookOf(Book);
  OKBookLastBalances(List,Seen);if(!List.IsEmpty()||S.BooksStarted)return;
  if(const auto* N=Now.Find(Book))for(const auto& A:*N){int64 Open=A.Value;for(const auto& P:S.Store.BookPending)if(P.Book==Book&&P.Account==A.Key&&Fresh(P))Open-=P.Amount;
   if(Open==0)continue;FOKBookEntry E;E.Serial=OKBookNextSerial();E.Account=A.Key;E.Amount=Open;E.Balance=Open;E.Category=TEXT("opening");E.Note=OKT(TEXT("ui.books.opening_balance_label"));Seen.Add(A.Key,Open);BookAppend(Book,MoveTemp(E));}
 };
 for(const auto& B:Now)Ensure(B.Key);
 S.BooksStarted=true;
 // The posts of the store in effect, each once (an old copy of the store may carry ones written already).
 for(const FOKBookPost& P:S.Store.BookPending){
  if(!Fresh(P))continue;S.BookFlushed.Add(P.Serial);Ensure(P.Book);
  int64& Bal=S.BookSeen[P.Book].FindOrAdd(P.Account);Bal+=P.Amount;
  FOKBookEntry E;E.Serial=P.Serial;E.Account=P.Account;E.Amount=P.Amount;E.Balance=Bal;E.Category=P.Category;E.Note=P.Note;BookAppend(P.Book,MoveTemp(E));
 }
 S.Store.BookPending.Reset();
 if(S.BookFlushed.Num()>4096)for(auto It=S.BookFlushed.CreateIterator();It;++It)if(*It<Floor)It.RemoveCurrent();
 // Whatever moved with no post: recorded as it is, with the bank's latest note as the likely reason.
 const FString BankNote=S.Store.Banking.Notes.IsEmpty()?FString():S.Store.Banking.Notes.Last();
 for(auto& Book:S.BookSeen){
  const TMap<FString,int64>* N=Now.Find(Book.Key);TSet<FString> Accounts;for(const auto& A:Book.Value)Accounts.Add(A.Key);if(N)for(const auto& A:*N)Accounts.Add(A.Key);
  for(const FString& A:Accounts){const int64 Want=N&&N->Contains(A)?(*N)[A]:0;int64& Have=Book.Value.FindOrAdd(A);if(Want==Have)continue;
   FOKBookEntry E;E.Serial=OKBookNextSerial();E.Account=A;E.Amount=Want-Have;E.Balance=Want;E.Category=TEXT("unclassified");
   E.Note=BankNote!=S.BookLastBankNote&&!BankNote.IsEmpty()?BankNote:FString(OKT(TEXT("ui.books.no_reason_recorded")));
   UE_LOG(LogTemp,Warning,TEXT("Street books: %s/%s moved %lld with no entry (%s)"),*Book.Key,*A,Want-Have,*E.Note);
   Have=Want;BookAppend(Book.Key,MoveTemp(E));}
 }
 S.BookLastBankNote=BankNote;
}

// A test's own change of balances, journaled as an adjustment (so "未分類" means a real gap).
static void OKBookAdjustTo(const FOKStreetStore& From,FOKStreetStore& To){
 TMap<FString,TMap<FString,int64>> A,B;OKBookBalances(From,A);OKBookBalances(To,B);
 for(const auto& Book:B)for(const auto& X:Book.Value){const int64 Was=A.Contains(Book.Key)?A[Book.Key].FindRef(X.Key):0;OKBookPost(To,Book.Key,X.Key,X.Value-Was,TEXT("adjust"),OKT(TEXT("ui.books.test_adjustment_label")));}
 for(const auto& Book:A)if(!B.Contains(Book.Key))for(const auto& X:Book.Value)OKBookPost(To,Book.Key,X.Key,-X.Value,TEXT("adjust"),OKT(TEXT("ui.books.test_adjustment_label")));
}
// Changes that reached the journal with no post since a serial (all books written in this session): a test's proof
// that every rule it ran told the books.
int32 AOKStreetSocialScene::BookGaps(int64 Since){
 auto& S=*State;S.BooksTickAt=0;TickBooks();int32 N=0;
 for(const auto& B:S.Books)for(const auto& E:B.Value)if(E.Serial>Since&&E.Category==TEXT("unclassified")){++N;UE_LOG(LogTemp,Warning,TEXT("Street books gap: %s/%s %lld %s"),*B.Key,*E.Account,E.Amount,*E.Note);}
 return N;
}
