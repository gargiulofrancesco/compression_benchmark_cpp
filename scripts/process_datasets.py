import os
import io
import requests
import tarfile
import gzip
import bz2
import json
from tqdm import tqdm

# Configuration

# Set data directory to project_root/data
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")

# Dataset 1: MS MARCO Queries
MSMARCO_QUERIES_URL = "https://msmarco.z22.web.core.windows.net/msmarcoranking/queries.tar.gz"
MSMARCO_QUERIES_OUTPUT = "msmarco_queries.json"

# Dataset 2: MS MARCO URLs
MSMARCO_URLS_URL = "https://msmarco.z22.web.core.windows.net/msmarcoranking/msmarco-docs.tsv.gz"
MSMARCO_URLS_OUTPUT = "msmarco_urls.json"

# Dataset 3: Amazon Book Titles
AMAZON_BOOKS_URL = "https://mcauleylab.ucsd.edu/public_datasets/data/amazon_2023/raw/meta_categories/meta_Books.jsonl.gz"
AMAZON_BOOKS_OUTPUT = "book_titles.json"

# Dataset 4: DBpedia Abstracts
DBPEDIA_URL = "https://databus.dbpedia.org/dbpedia/text/short-abstracts/2022.12.01/short-abstracts_lang=en.ttl.bz2"
DBPEDIA_OUTPUT = "dbpedia_abstracts.json"
DBPEDIA_LIMIT = 1_000_000

def ensure_data_dir():
    if not os.path.exists(DATA_DIR):
        print(f"Creating directory: {DATA_DIR}")
        os.makedirs(DATA_DIR)

def download_to_file(url, local_filename, desc="Downloading"):
    """
    Downloads a file from URL to a local path with a progress bar.
    """
    response = requests.get(url, stream=True)
    response.raise_for_status()
    
    total_size = int(response.headers.get('content-length', 0))
    block_size = 8192  # 8 KB chunks
    
    with open(local_filename, "wb") as f, tqdm(total=total_size, unit='iB', unit_scale=True, desc=desc) as pbar:
        for chunk in response.iter_content(block_size):
            f.write(chunk)
            pbar.update(len(chunk))
    
    return local_filename

def process_msmarco_queries():
    output_path = os.path.join(DATA_DIR, MSMARCO_QUERIES_OUTPUT)
    
    # Skip if already done
    if os.path.exists(output_path):
        print(f"[Skip] {MSMARCO_QUERIES_OUTPUT} already exists.")
        return

    print(f"\n[1/4] Processing MS MARCO Queries...")
    
    # Temporary path for the downloaded tarball
    temp_archive_path = os.path.join(DATA_DIR, "temp_queries.tar.gz")

    try:
        # Download to Disk
        download_to_file(MSMARCO_QUERIES_URL, temp_archive_path, desc="  Fetching tarball")

        all_queries = []
        
        # Extract & Parse
        print("  Extracting queries from archive...")
        # Open the local file directly
        with tarfile.open(temp_archive_path, mode="r:gz") as tar:
            for member in tar.getmembers():
                if member.name.endswith(".tsv"):
                    f_in = tar.extractfile(member)
                    if f_in:
                        with io.TextIOWrapper(f_in, encoding="utf-8") as text_file:
                            for line in tqdm(text_file, desc=f"  Parsing {member.name}", unit=" lines"):
                                parts = line.split("\t", 1)
                                if len(parts) >= 2:
                                    query = parts[1].strip()
                                    if query:
                                        all_queries.append(query)


        # Save Output
        print(f"  Saving to {output_path}...")
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(all_queries, f, ensure_ascii=False, indent=2)
            
        print(f"  ✓ Done. Saved {len(all_queries)} queries.")

    except Exception as e:
        print(f"  ❌ Error: {e}")
    
    finally:
        # Cleanup: Delete the downloaded archive
        if os.path.exists(temp_archive_path):
            print(f"  Cleaning up: Removing {temp_archive_path}")
            os.remove(temp_archive_path)

def process_msmarco_urls():
    output_path = os.path.join(DATA_DIR, MSMARCO_URLS_OUTPUT)
    
    if os.path.exists(output_path):
        print(f"[Skip] {MSMARCO_URLS_OUTPUT} already exists.")
        return

    print(f"\n[2/4] Processing MS MARCO URLs...")
    
    temp_archive_path = os.path.join(DATA_DIR, "temp_docs.tsv.gz")

    try:
        # Download file to disk
        download_to_file(MSMARCO_URLS_URL, temp_archive_path, desc="  Fetching corpus")

        urls = []
        
        # Process line-by-line using gzip
        print("  Scanning corpus for URLs...")
        with gzip.open(temp_archive_path, mode="rt", encoding="utf-8") as f_in:
            for line in tqdm(f_in, desc="  Extracting", unit=" docs"):
                # TSV Format: docid <tab> url <tab> title <tab> body
                # We use maxsplit=2 to STOP splitting after the URL. 
                parts = line.split("\t", 2)
                
                if len(parts) >= 2:
                    url = parts[1]
                    # Simple validation
                    if url.startswith("http"):
                        urls.append(url)
        
        # Save
        print(f"  Saving to {output_path}...")
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(urls, f, ensure_ascii=False, indent=2)
            
        print(f"  ✓ Done. Saved {len(urls)} URLs.")

    except Exception as e:
        print(f"  ❌ Error: {e}")
        
    finally:
        # Cleanup
        if os.path.exists(temp_archive_path):
            print(f"  Cleaning up: Removing {temp_archive_path}")
            os.remove(temp_archive_path)

def process_amazon_titles():
    output_path = os.path.join(DATA_DIR, AMAZON_BOOKS_OUTPUT)
    
    if os.path.exists(output_path):
        print(f"[Skip] {AMAZON_BOOKS_OUTPUT} already exists.")
        return

    print(f"\n[3/4] Processing Amazon Book Titles...")
    temp_archive_path = os.path.join(DATA_DIR, "temp_amazon_books.jsonl.gz")

    try:
        # Download
        download_to_file(AMAZON_BOOKS_URL, temp_archive_path, desc="  Fetching Amazon Metadata")

        titles = []
        
        # Stream Process
        print("  Extracting titles...")
        with gzip.open(temp_archive_path, mode="rt", encoding="utf-8") as f_in:
            for line in tqdm(f_in, desc="  Parsing", unit=" items"):
                try:
                    data = json.loads(line)
                    title = data.get("title", "").strip()
                    if title:
                        titles.append(title)
                except json.JSONDecodeError:
                    continue
        
        # Save
        print(f"  Saving to {output_path}...")
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(titles, f, ensure_ascii=False, indent=2)
            
        print(f"  ✓ Done. Saved {len(titles)} titles.")

    except Exception as e:
        print(f"  ❌ Error: {e}")
        
    finally:
        if os.path.exists(temp_archive_path):
            print(f"  Cleaning up: Removing {temp_archive_path}")
            os.remove(temp_archive_path)

def process_dbpedia_abstracts():
    output_path = os.path.join(DATA_DIR, DBPEDIA_OUTPUT)

    if os.path.exists(output_path):
        print(f"[Skip] {DBPEDIA_OUTPUT} already exists.")
        return

    print(f"\n[4/4] Processing DBpedia Abstracts (Limit: {DBPEDIA_LIMIT})...")
    
    # Download path
    temp_archive_path = os.path.join(DATA_DIR, "temp_dbpedia.ttl.bz2")

    try:
        download_to_file(DBPEDIA_URL, temp_archive_path, desc="  Fetching DBpedia Dumps")
        
        abstracts = []
        count = 0

        print("  Parsing N-Triples and extracting text...")
        # Use bz2.open to read the compressed stream directly
        with bz2.open(temp_archive_path, mode="rt", encoding="utf-8") as f_in:
            pbar = tqdm(total=DBPEDIA_LIMIT, desc="  Extracting", unit=" items")
            
            for line in f_in:
                # Basic validation: Skip comments or empty lines
                if line.startswith('#') or not line.strip():
                    continue

                # Parse N-Triples: <subj> <pred> "object"@en .
                # Look for the first quote and the last quote ("@en)
                start_index = line.find('"')
                end_index = line.rfind('"@en')

                if start_index != -1 and end_index != -1 and end_index > start_index:
                    # Extract the escaped string content
                    raw_text = line[start_index + 1 : end_index]
                    
                    abstracts.append(raw_text)
                    count += 1
                    pbar.update(1)

                    if count >= DBPEDIA_LIMIT:
                        break
            
            pbar.close()

        print(f"  Saving to {output_path}...")
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(abstracts, f, ensure_ascii=False, indent=2)

        print(f"  ✓ Done. Saved {len(abstracts)} abstracts.")

    except Exception as e:
        print(f"  ❌ Error: {e}")

    finally:
        if os.path.exists(temp_archive_path):
            print(f"  Cleaning up: Removing {temp_archive_path}")
            os.remove(temp_archive_path)

def main():
    ensure_data_dir()
    
    # 1. Queries
    process_msmarco_queries()
    
    # 2. URLs
    process_msmarco_urls()
    
    # 3. Amazon Titles
    process_amazon_titles()

    # 4. DBpedia
    process_dbpedia_abstracts()

    print("\nAll tasks completed.")

if __name__ == "__main__":
    main()